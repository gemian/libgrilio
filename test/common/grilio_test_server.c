/*
 * Copyright (C) 2015-2017 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "grilio_test_server.h"
#include "grilio_p.h"

#include <gutil_log.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define server_fd fd[0]
#define client_fd fd[1]

struct grilio_test_server {
    int fd[2];
    GIOChannel* io_channel;
    guint read_watch_id;
    guint write_watch_id;
    GByteArray* read_buf;
    char sub[4];
    int sub_len;
    char buf[1024];
    int write_chunk;
    int write_pos;
    GByteArray* write_data;
    GSList* handlers;
    GHashTable* code_handlers;
};

typedef struct grilio_test_server_handler {
    GRilIoTestRequestFunc fn;
    void* user_data;
} GRilIoTestRequestHandler;

static const guchar UNSOL_RIL_CONNECTED[] = {
    0x00, 0x00, 0x00, 0x10,
    0x01, 0x00, 0x00, 0x00, 0x0a, 0x04, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, GRILIO_RIL_VERSION, 0x00, 0x00, 0x00
};

static
gboolean
grilio_test_server_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data);

static
gboolean
grilio_test_server_ready_to_write(
    GRilIoTestServer* server)
{
    return (server->sub_len == 4);
}

static
void
grilio_test_server_start_writing(
    GRilIoTestServer* server)
{
    if (!server->write_watch_id) {
            server->write_watch_id = g_io_add_watch(server->io_channel,
                G_IO_OUT, grilio_test_server_write_callback, server);
    }
}

static
gboolean
grilio_test_server_write(
    GRilIoTestServer* server)
{
    GError* error = NULL;
    if (server->write_pos < server->write_data->len) {
        gsize bytes_written = 0;
        int len = server->write_data->len - server->write_pos;
        if (server->write_chunk && len > server->write_chunk) {
            len = server->write_chunk;
        }
        g_io_channel_write_chars(server->io_channel,
            (void*)(server->write_data->data + server->write_pos), len,
            &bytes_written, &error);
        if (error) {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
            return FALSE;
        }
        server->write_pos += bytes_written;
    }
    if (server->write_pos < server->write_data->len) {
        return TRUE;
    } else {
        server->write_data = g_byte_array_set_size(server->write_data, 0);
        server->write_pos = 0;
        return FALSE;
    }
}

static
void
grilio_test_server_call_handlers(
    GSList* list,
    guint code,
    guint id,
    const void* data,
    guint len)
{
    while (list) {
        GRilIoTestRequestHandler* handler = list->data;
        handler->fn(code, id, data, len, handler->user_data);
        list = list->next;
    }
}

static
gboolean
grilio_test_server_read(
    GRilIoTestServer* server)
{
    GError* error = NULL;
    gsize bytes_read;
    if (server->sub_len < 4) {
        bytes_read = 0;
        g_io_channel_read_chars(server->io_channel,
            server->sub + server->sub_len,
            4 - server->sub_len, &bytes_read, &error);
        if (error) {
            GERR("%s", GERRMSG(error));
            g_error_free(error);
            return FALSE;
        }
        GVERBOSE("Received %lu bytes", (unsigned long)bytes_read);
        server->sub_len += bytes_read;
        if (server->sub_len == 4) {
            GDEBUG("Subscription %.4s", server->sub);
            grilio_test_server_start_writing(server);
        } else {
            return TRUE;
        }
    }
    bytes_read = 0;
    g_io_channel_read_chars(server->io_channel, server->buf,
        sizeof(server->buf), &bytes_read, &error);
    if (error) {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
        return FALSE;
    }
    if (bytes_read) {
        GVERBOSE("Received %lu bytes", (unsigned long)bytes_read);
        g_byte_array_append(server->read_buf, (void*)server->buf, bytes_read);
        if (server->read_buf->len > 4) {
            const guint32* header = (guint32*)server->read_buf->data;
            const guint32 len = GUINT32_FROM_BE(header[0]);
            GASSERT(len >= 8);
            if (server->read_buf->len >= len + 4) {
                const guint32 code = GUINT32_FROM_RIL(header[1]);
                const guint32 id = GUINT32_FROM_RIL(header[2]);
                const guint32 data_len = len - 8;
                const void* data = server->read_buf->data +
                    RIL_REQUEST_HEADER_SIZE;
                GDEBUG("Request %u, id=%u, len=%u", code, id, data_len);
                grilio_test_server_call_handlers(server->handlers, code, id,
                    data, data_len);
                if (server->code_handlers) {
                    grilio_test_server_call_handlers(g_hash_table_lookup(
                        server->code_handlers, GINT_TO_POINTER(code)), code,
                        id, data, data_len);
                }
                g_byte_array_remove_range(server->read_buf, 0, len + 4);
            }
        }
    }
    return TRUE;
}

static
gboolean
grilio_test_server_read_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    GRilIoTestServer* server = data;
    gboolean ok = (condition & G_IO_IN) && grilio_test_server_read(server);
    if (!ok) server->read_watch_id = 0;
    return TRUE;
}

static
gboolean
grilio_test_server_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    GRilIoTestServer* server = data;
    gboolean ok = (condition & G_IO_OUT) && grilio_test_server_write(server);
    if (!ok) server->write_watch_id = 0;
    return ok;
}

GRilIoTestServer*
grilio_test_server_new()
{
    GRilIoTestServer* server = g_new0(GRilIoTestServer, 1);
    socketpair(AF_UNIX, SOCK_STREAM, 0, server->fd);
    server->io_channel = g_io_channel_unix_new(server->server_fd);
    server->read_buf = g_byte_array_new();
    server->write_data = g_byte_array_new();
    g_byte_array_append(server->write_data, UNSOL_RIL_CONNECTED,
        sizeof(UNSOL_RIL_CONNECTED));
    g_io_channel_set_flags(server->io_channel, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_encoding(server->io_channel, NULL, NULL);
    g_io_channel_set_buffered(server->io_channel, FALSE);
    server->read_watch_id = g_io_add_watch(server->io_channel, G_IO_IN,
        grilio_test_server_read_callback, server);
    return server;
}

void
grilio_test_server_free(
    GRilIoTestServer* server)
{
    if (server->write_watch_id) g_source_remove(server->write_watch_id);
    if (server->read_watch_id) g_source_remove(server->read_watch_id);
    if (server->code_handlers) g_hash_table_destroy(server->code_handlers);
    g_slist_free_full(server->handlers, g_free);
    g_byte_array_unref(server->write_data);
    g_byte_array_unref(server->read_buf);
    g_io_channel_unref(server->io_channel);
    if (server->server_fd >= 0) close(server->server_fd);
    close(server->client_fd);
    g_free(server);
}

int
grilio_test_server_fd(
    GRilIoTestServer* server)
{
    return server->client_fd;
}

void
grilio_test_server_set_chunk(
    GRilIoTestServer* server,
    int chunk)
{
    server->write_chunk = chunk;
}

void
grilio_test_server_shutdown(
    GRilIoTestServer* server)
{
    if (server->server_fd >= 0) {
        shutdown(server->server_fd, SHUT_RDWR);
        close(server->server_fd);
        server->server_fd= -1;
    }
}

void
grilio_test_server_add_data(
    GRilIoTestServer* server,
    const void* data,
    guint len)
{
    g_byte_array_append(server->write_data, data, len);
    if (grilio_test_server_ready_to_write(server)) {
        grilio_test_server_start_writing(server);
    }
}

void
grilio_test_server_add_response_data(
    GRilIoTestServer* server,
    guint id,
    guint status,
    const void* data,
    guint len)
{
    guint32* header;
    guint oldlen = server->write_data->len;
    g_byte_array_set_size(server->write_data, oldlen + 16);
    header = (guint32*)(server->write_data->data + oldlen);
    header[0] = GUINT32_TO_BE(len + RIL_RESPONSE_HEADER_SIZE);
    header[1] = 0;  /* Solicited Response */
    header[2] = GUINT32_TO_RIL(id);
    header[3] = GUINT32_TO_RIL(status);
    if (len) g_byte_array_append(server->write_data, data, len);
    if (grilio_test_server_ready_to_write(server)) {
        grilio_test_server_start_writing(server);
    }
}

void
grilio_test_server_add_response(
    GRilIoTestServer* server,
    GRilIoRequest* resp,
    guint id,
    guint status)
{
    grilio_test_server_add_response_data(server, id, status,
        grilio_request_data(resp), grilio_request_size(resp));
}

static
void
grilio_test_server_free_handlers(
    gpointer data)
{
    g_slist_free_full(data, g_free);
}

void
grilio_test_server_add_request_func(
    GRilIoTestServer* server,
    guint code,
    GRilIoTestRequestFunc fn,
    void* user_data)
{
    GRilIoTestRequestHandler* handler = g_new(GRilIoTestRequestHandler, 1);
    handler->fn = fn;
    handler->user_data = user_data;
    if (code) {
        GSList* handlers = NULL;
        gpointer key = GINT_TO_POINTER(code);
        if (server->code_handlers) {
            handlers = g_hash_table_lookup(server->code_handlers, key);
        } else {
            handlers = NULL;
            server->code_handlers = g_hash_table_new_full(g_direct_hash,
                g_direct_equal, NULL, grilio_test_server_free_handlers);
        }
        g_hash_table_insert(server->code_handlers, key,
            g_slist_append(handlers, handler));
    } else {
        server->handlers = g_slist_append(server->handlers, handler);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
