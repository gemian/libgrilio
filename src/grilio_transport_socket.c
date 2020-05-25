/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#include "grilio_transport_impl.h"
#include "grilio_p.h"
#include "grilio_parser.h"

#define GLOG_MODULE_NAME grilio_transport_socket_log
#include <gutil_log.h>

#include <gio/gio.h>

#include <gio/gio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

/* Log module */
GLOG_MODULE_DEFINE2("grilio-socket", GRILIO_LOG_MODULE);

/* This limit is more or less arbitrary */
#define RIL_MAX_PACKET_LEN (0x8000)

/* RIL constants */
#define RIL_SUB_LEN (4)
#define RIL_MIN_HEADER_SIZE RIL_ACK_HEADER_SIZE

typedef GRilIoTransportClass GRilIoTransportSocketClass;
typedef struct grilio_transport_socket {
    GRilIoTransport parent;
    GIOChannel* io_channel;
    guint read_watch_id;
    guint write_watch_id;
    guint write_error_id;
    GError* write_error;
    gboolean disconnected;

    /* Subscription */
    gchar sub[RIL_SUB_LEN];
    guint sub_pos;

    /* Send */
    guint8 send_header[RIL_REQUEST_HEADER_SIZE + 4]; /* Including length */
    guint send_header_pos;
    guint send_pos;
    GRilIoRequest* send_req;

    /* Receive */
    gchar read_len_buf[4];
    guint read_len_pos;
    guint read_len;
    guint read_buf_pos;
    guint read_buf_alloc;
    gchar* read_buf;
} GRilIoTransportSocket;

G_DEFINE_TYPE(GRilIoTransportSocket, grilio_transport_socket,
    GRILIO_TYPE_TRANSPORT)

#define PARENT_CLASS grilio_transport_socket_parent_class
#define GRILIO_TYPE_TRANSPORT_SOCKET (grilio_transport_socket_get_type())
#define GRILIO_TRANSPORT_SOCKET(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GRILIO_TYPE_TRANSPORT_SOCKET, \
    GRilIoTransportSocket)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
grilio_transport_socket_shutdown_io(
    GRilIoTransportSocket* self,
    gboolean flush)
{
    if (self->read_watch_id) {
        g_source_remove(self->read_watch_id);
        self->read_watch_id = 0;
    }
    if (self->write_watch_id) {
        g_source_remove(self->write_watch_id);
        self->write_watch_id = 0;
    }
    if (self->io_channel) {
        g_io_channel_shutdown(self->io_channel, flush, NULL);
        g_io_channel_unref(self->io_channel);
        self->io_channel = NULL;
    }
}

static
void
grilio_transport_socket_disconnected(
    GRilIoTransportSocket* self)
{
    GRilIoTransport* transport = &self->parent;

    transport->connected = FALSE;
    if (!self->disconnected) {
        self->disconnected = TRUE;
        grilio_transport_signal_disconnected(transport);
    }
}

/*==========================================================================*
 * Read
 *==========================================================================*/

static
void
grilio_transport_socket_handle_read_error(
    GRilIoTransportSocket* self,
    GError* error)
{
    GRilIoTransport* transport = &self->parent;

    GERR("%sread failed: %s", transport->log_prefix, GERRMSG(error));

    /*
     * Zero watch id to avoid removing it twice. This one is going to be
     * freed when we return FALSE from the callback.
     */
    self->read_watch_id = 0;
    grilio_transport_shutdown(transport, FALSE);
    grilio_transport_signal_read_error(transport, error);
    g_error_free(error);
}

static
void
grilio_transport_socket_handle_eof(
    GRilIoTransportSocket* self)
{
    GRilIoTransport* transport = &self->parent;

    GERR("%shangup", transport->log_prefix);

    /*
     * Zero watch id to avoid removing it twice. This one is going to be
     * freed when we return FALSE from the callback.
     */
    self->read_watch_id = 0;
    grilio_transport_shutdown(transport, FALSE);
}

static
void
grilio_transport_socket_signal_response(
    GRilIoTransportSocket* self,
    GRILIO_RESPONSE_TYPE type)
{
    const guint32* buf = (guint32*)self->read_buf;
    const uint offset = RIL_RESPONSE_HEADER_SIZE;

    grilio_transport_signal_response(&self->parent, type,
        GUINT32_FROM_RIL(buf[1]) /* id */,
        GUINT32_FROM_RIL(buf[2]) /* status */,
        self->read_buf + offset,
        self->read_len - offset);
}

static
gboolean
grilio_transport_socket_handle_solicited(
    GRilIoTransportSocket* self)
{
    if (self->read_len >= RIL_RESPONSE_HEADER_SIZE) {
        grilio_transport_socket_signal_response(self,
            GRILIO_RESPONSE_SOLICITED);
        return TRUE;
    } else {
        grilio_transport_socket_handle_read_error(self,
            g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Response too short (%u bytes)", self->read_len));
        return FALSE;
    }
}

static
gboolean
grilio_transport_socket_handle_solicited_ack(
    GRilIoTransportSocket* self)
{
    const guint32* buf = (guint32*)self->read_buf;

    grilio_transport_signal_response(&self->parent,
        GRILIO_RESPONSE_SOLICITED_ACK,
        GUINT32_FROM_RIL(buf[1]),
        RIL_E_SUCCESS, NULL, 0);
    return TRUE;
}

static
gboolean
grilio_transport_socket_handle_solicited_ack_exp(
    GRilIoTransportSocket* self)
{
    if (self->read_len >= RIL_RESPONSE_HEADER_SIZE) {
        grilio_transport_socket_signal_response(self,
            GRILIO_RESPONSE_SOLICITED_ACK_EXP);
        return TRUE;
    } else {
        grilio_transport_socket_handle_read_error(self,
            g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Response too short (%u bytes)", self->read_len));
        return FALSE;
    }
}

static
void
grilio_transport_socket_connected(
    GRilIoTransportSocket* self)
{
    GRilIoTransport* transport = &self->parent;
    GRilIoParser parser;
    const guint off = RIL_UNSOL_HEADER_SIZE;
    guint num = 0;

    GASSERT(!transport->connected);
    grilio_parser_init(&parser, self->read_buf + off, self->read_len - off);
    if (grilio_parser_get_uint32(&parser, &num) && num == 1 &&
        grilio_parser_get_uint32(&parser, &transport->ril_version)) {
        GDEBUG("Connected, RIL version %u", transport->ril_version);
        transport->connected = TRUE;
        grilio_transport_signal_connected(transport);
    } else {
        /* Terminate the connection? */
        GERR("Failed to parse RIL_UNSOL_RIL_CONNECTED");
    }
}

static
void
grilio_transport_socket_handle_indication(
    GRilIoTransportSocket* self,
    GRILIO_INDICATION_TYPE type)
{
    /* The caller has checked the length */
    const guint32* buf = (guint32*)self->read_buf;
    const guint32 code = GUINT32_FROM_RIL(buf[1]);
    const uint offset = RIL_UNSOL_HEADER_SIZE;

    grilio_transport_signal_indication(&self->parent, type, code,
        self->read_buf + offset, self->read_len - offset);

    /* Handle RIL_UNSOL_RIL_CONNECTED */
    if (code == RIL_UNSOL_RIL_CONNECTED) {
        grilio_transport_socket_connected(self);
    }
}

gboolean
grilio_transport_socket_handle_packet(
    GRilIoTransportSocket* self)
{
    if (self->read_len >= RIL_MIN_HEADER_SIZE) {
        const guint32* buf = (guint32*)self->read_buf;
        const RIL_PACKET_TYPE type = GUINT32_FROM_RIL(buf[0]);

        switch (type) {
        case RIL_PACKET_TYPE_SOLICITED:
            return grilio_transport_socket_handle_solicited(self);
        case RIL_PACKET_TYPE_SOLICITED_ACK:
            return grilio_transport_socket_handle_solicited_ack(self);
        case RIL_PACKET_TYPE_SOLICITED_ACK_EXP:
            return grilio_transport_socket_handle_solicited_ack_exp(self);
        case RIL_PACKET_TYPE_UNSOLICITED:
            grilio_transport_socket_handle_indication(self,
                GRILIO_INDICATION_UNSOLICITED);
           return TRUE;
        case RIL_PACKET_TYPE_UNSOLICITED_ACK_EXP:
            grilio_transport_socket_handle_indication(self,
                GRILIO_INDICATION_UNSOLICITED_ACK_EXP);
           return TRUE;
        default:
            /* Ignore unknown packets */
            GWARN("Unexpected packet type id %d", type);
            return TRUE;
        }
    } else {
        grilio_transport_socket_handle_read_error(self,
            g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Packet too short (%u bytes)", self->read_len));
        return FALSE;
    }
}

static
gboolean
grilio_transport_socket_read_chars(
    GRilIoTransportSocket* self,
    gchar* buf,
    gsize count,
    gsize* bytes_read)
{
    GError* error = NULL;
    GIOStatus status = g_io_channel_read_chars(self->io_channel, buf,
        count, bytes_read, &error);

    if (error) {
        grilio_transport_socket_handle_read_error(self, error);
        return FALSE;
    } else if (status == G_IO_STATUS_EOF) {
        grilio_transport_socket_handle_eof(self);
        return FALSE;
    } else {
        return TRUE;
    }
}

static
gboolean
grilio_transport_socket_read(
    GRilIoTransportSocket* self)
{
    gsize bytes_read;

    /* Length */
    if (self->read_len_pos < 4) {
        if (!grilio_transport_socket_read_chars(self,
            self->read_len_buf + self->read_len_pos,
            4 - self->read_len_pos, &bytes_read)) {
            return FALSE;
        }
        self->read_len_pos += bytes_read;
        GASSERT(self->read_len_pos <= 4);
        if (self->read_len_pos < 4) {
            /* Need more bytes */
            return TRUE;
        } else {
            /* We have finished reading the length (in Big Endian) */
            const guint32* len = (guint32*)self->read_len_buf;
            self->read_len = GUINT32_FROM_BE(*len);
            GASSERT(self->read_len <= RIL_MAX_PACKET_LEN);
            if (self->read_len <= RIL_MAX_PACKET_LEN) {
                /* Reset buffer read position */
                self->read_buf_pos = 0;
                /* Allocate enough space for the entire packet */
                if (self->read_buf_alloc < self->read_len) {
                    g_free(self->read_buf);
                    self->read_buf_alloc = self->read_len;
                    self->read_buf = g_malloc(self->read_buf_alloc);
                }
            } else {
                /* Message is too long or stream is broken */
                return FALSE;
            }
        }
    }

    /* Packet body */
    if (self->read_buf_pos < self->read_len) {
        if (!grilio_transport_socket_read_chars(self,
            self->read_buf + self->read_buf_pos,
            self->read_len - self->read_buf_pos, &bytes_read)) {
            return FALSE;
        }
        self->read_buf_pos += bytes_read;
        GASSERT(self->read_buf_pos <= self->read_len);
        if (self->read_buf_pos < self->read_len) {
            /* Need more bytes */
            return TRUE;
        }
    }

    /* Reset the reading position to indicate that we are ready to start
     * receiving the next packet */
    self->read_len_pos = 0;

    /* We have finished reading the entire packet */
    return grilio_transport_socket_handle_packet(self);
}

static
gboolean
grilio_transport_socket_read_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    gboolean result;
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(user_data);

    g_object_ref(self);
    if ((condition & G_IO_IN) && grilio_transport_socket_read(self)) {
        result = G_SOURCE_CONTINUE;
    } else {
        self->read_watch_id = 0;
        result = G_SOURCE_REMOVE;
    }
    g_object_unref(self);
    return result;
}

/*==========================================================================*
 * Write
 *==========================================================================*/

static
gboolean
grilio_transport_socket_write_error_cb(
    gpointer user_data)
{
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(user_data);
    GRilIoTransport* transport = &self->parent;
    GError* error = self->write_error;

    GASSERT(self->write_error_id);
    self->write_error_id = 0;
    self->write_error = NULL;
    grilio_transport_signal_write_error(transport, error);
    grilio_transport_socket_disconnected(self);
    g_error_free(error);
    return G_SOURCE_REMOVE;
}

static
void
grilio_transport_socket_handle_write_error(
    GRilIoTransportSocket* self,
    GError* error)
{
    GRilIoTransport* transport = &self->parent;

    GERR("%swrite failed: %s", transport->log_prefix, GERRMSG(error));

    /*
     * Zero watch id to avoid removing it twice. This one is going
     * to be freed when we return FALSE from the callback.
     */
    self->write_watch_id = 0;

    /* Don't emit DISCONNECTED signal just yet */
    grilio_transport_socket_shutdown_io(self, FALSE);

    /*
     * It's dangerous to emit write error signal right away. The signal
     * handler may release the last reference to the caller. We should
     * emit the signal on a fresh stack
     */
    if (self->write_error) {
        g_error_free(self->write_error);
    }

    /* grilio_transport_socket_handle_write_error_cb will free the error */
    self->write_error = error;
    if (!self->write_error_id) {
        self->write_error_id = g_idle_add
            (grilio_transport_socket_write_error_cb, self);
    }
}

static
gboolean
grilio_transport_socket_write_chars(
    GRilIoTransportSocket* self,
    const void* buf,
    gssize count,
    gsize* bytes_written,
    GError** error)
{
    const GIOStatus status = g_io_channel_write_chars(self->io_channel,
        buf, count, bytes_written, error);

    if (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN) {
        return TRUE;
    } else {
        GASSERT(*error);
        return FALSE;
    }
}

static
gboolean
grilio_transport_socket_write(
    GRilIoTransportSocket* self,
    GError** error)
{
    GRilIoTransport* transport = &self->parent;
    GRilIoRequest* req = self->send_req;
    guint datalen;

    if (self->sub_pos < RIL_SUB_LEN) {
        gsize bytes_written = 0;

        if (!grilio_transport_socket_write_chars(self,
            self->sub + self->sub_pos,
            RIL_SUB_LEN - self->sub_pos,
            &bytes_written, error)) {
            return FALSE;
        }
        self->sub_pos += bytes_written;
        GASSERT(self->sub_pos <= RIL_SUB_LEN);
        if (self->sub_pos < RIL_SUB_LEN) {
            /* Will have to wait */
            return TRUE;
        }
        GDEBUG("%ssubscribed for %c%c%c%c", transport->log_prefix,
            self->sub[0], self->sub[1], self->sub[2], self->sub[3]);
    }

    if (!transport->connected) {
        GVERBOSE("%snot connected", transport->log_prefix);
        return FALSE;
    }

    if (!req) {
        /* There is nothing to send, remove the watch */
        GVERBOSE("%shas nothing to send", transport->log_prefix);
        return FALSE;
    }

    /* Send the header */
    if (self->send_header_pos < sizeof(self->send_header)) {
        gsize bytes_written = 0;

        if (!grilio_transport_socket_write_chars(self,
            self->send_header + self->send_header_pos,
            sizeof(self->send_header) - self->send_header_pos,
            &bytes_written, error)) {
            return FALSE;
        }
        self->send_header_pos += bytes_written;
        GASSERT(self->send_header_pos <= sizeof(self->send_header));
        if (self->send_header_pos < sizeof(self->send_header)) {
            /* Will have to wait */
            return TRUE;
        }
    }

    /* Send the data */
    datalen = grilio_request_size(req);
    if (self->send_pos < datalen) {
        gsize bytes_written = 0;

        if (!grilio_transport_socket_write_chars(self,
            req->bytes->data + self->send_pos,
            datalen - self->send_pos,
            &bytes_written, error)) {
            return FALSE;
        }
        self->send_pos += bytes_written;
        GASSERT(self->send_pos <= datalen);
        if (self->send_pos < datalen) {
            /* Will have to wait */
            return TRUE;
        }
    }

    /* The request has been sent */
    grilio_request_unref(req);
    self->send_req = NULL;
    return TRUE;
}

static
gboolean
grilio_transport_socket_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer user_data)
{
    gboolean result = G_SOURCE_REMOVE;
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(user_data);
    GError* error = NULL;

    g_object_ref(self);
    if (condition & G_IO_OUT) {
        GRilIoTransport* transport = &self->parent;
        GRilIoRequest* req = grilio_request_ref(self->send_req);

        if (grilio_transport_socket_write(self, &error)) {
            if (self->send_req) {
                /* We have successfully written part of the packet */
                GASSERT(self->send_req == req);
                result = G_SOURCE_CONTINUE;
            } else if (req) {
                grilio_transport_signal_request_sent(transport, req);
            }
        }
        grilio_request_unref(req);
    }
    if (result == G_SOURCE_REMOVE) {
        self->write_watch_id = 0;
    }
    if (error) {
        grilio_transport_socket_handle_write_error(self, error);
    }
    g_object_unref(self);
    return result;
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
GRILIO_SEND_STATUS
grilio_transport_socket_send(
    GRilIoTransport* transport,
    GRilIoRequest* req,
    guint code)
{
    GRILIO_SEND_STATUS status = GRILIO_SEND_ERROR;
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(transport);

    GASSERT(!self->send_req);
    if (!self->send_req && req && self->io_channel) {
        GError* error = NULL;
        guint32* header = (guint32*)self->send_header;
        const guint datalen = grilio_request_size(req);
        const guint serial = grilio_request_serial(req);

        /* Length includes the header excluding the length iteslf */
        header[0] = GINT32_TO_BE(datalen + RIL_REQUEST_HEADER_SIZE);
        header[1] = GUINT32_TO_RIL(code);
        header[2] = GUINT32_TO_RIL(serial);

        self->send_req = grilio_request_ref(req);
        self->send_header_pos = 0;
        self->send_pos = 0;

        if (grilio_transport_socket_write(self, &error)) {
            if (!self->send_req) {
                status = GRILIO_SEND_OK;
            } else {
                status = GRILIO_SEND_PENDING;
                if (!self->write_watch_id) {
                    GVERBOSE("%sscheduling write", transport->log_prefix);
                    self->write_watch_id = g_io_add_watch(self->io_channel,
                        G_IO_OUT, grilio_transport_socket_write_callback,
                        self);
                }
            }
        }
        if (error) {
            grilio_transport_socket_handle_write_error(self, error);
        }
    }
    return status;
}

static
void
grilio_transport_socket_shutdown(
    GRilIoTransport* transport,
    gboolean flush)
{
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(transport);

    grilio_transport_socket_shutdown_io(self, flush);
    grilio_transport_socket_disconnected(self);
}

/*==========================================================================*
 * API
 *==========================================================================*/

GRilIoTransport*
grilio_transport_socket_new(
    int fd,
    const char* sub,
    gboolean can_close)
{
    if (G_LIKELY(fd >= 0 && (!sub || strlen(sub) == RIL_SUB_LEN))) {
        GRilIoTransportSocket* self = g_object_new
            (GRILIO_TYPE_TRANSPORT_SOCKET, NULL);
        GRilIoTransport* transport = &self->parent;

        self->io_channel = g_io_channel_unix_new(fd);
        if (self->io_channel) {
            g_io_channel_set_flags(self->io_channel, G_IO_FLAG_NONBLOCK, NULL);
            g_io_channel_set_encoding(self->io_channel, NULL, NULL);
            g_io_channel_set_buffered(self->io_channel, FALSE);
            g_io_channel_set_close_on_unref(self->io_channel, can_close);
            self->read_watch_id = g_io_add_watch(self->io_channel,
                G_IO_IN, grilio_transport_socket_read_callback, self);
            if (sub) {
                memcpy(self->sub, sub, RIL_SUB_LEN);
                self->write_watch_id = g_io_add_watch(self->io_channel,
                    G_IO_OUT, grilio_transport_socket_write_callback, self);
            } else {
                self->sub_pos = RIL_SUB_LEN;
            }
            return transport;
        }
        grilio_transport_unref(transport);
    }
    return NULL;
}

GRilIoTransport*
grilio_transport_socket_new_path(
    const char* path,
    const char* sub)
{
    if (G_LIKELY(path)) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                GRilIoTransport* transport =
                    grilio_transport_socket_new(fd, sub, TRUE);

                if (transport) {
                    GDEBUG("Opened %s", path);
                    return transport;
                }
            } else {
		GERR("Can't connect to RILD: %s", strerror(errno));
            }
            close(fd);
        } else {
            GERR("Can't create unix socket: %s", strerror(errno));
        }
    }
    return NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
grilio_transport_socket_init(
    GRilIoTransportSocket* self)
{
}

static
void
grilio_transport_socket_finalize(
    GObject* object)
{
    GRilIoTransportSocket* self = GRILIO_TRANSPORT_SOCKET(object);

    grilio_transport_socket_shutdown(&self->parent, FALSE);
    grilio_request_unref(self->send_req);
    if (self->write_error_id) {
        g_source_remove(self->write_error_id);
    }
    if (self->write_error) {
        g_error_free(self->write_error);
    }
    g_free(self->read_buf);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
grilio_transport_socket_class_init(
    GRilIoTransportSocketClass* klass)
{
    klass->send = grilio_transport_socket_send;
    klass->shutdown = grilio_transport_socket_shutdown;
    G_OBJECT_CLASS(klass)->finalize = grilio_transport_socket_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
