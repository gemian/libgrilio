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

#include "grilio_p.h"
#include "grilio_parser.h"
#include "grilio_log.h"

#include <gutil_misc.h>

#include <gio/gio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define GRILIO_MAX_PACKET_LEN (0x8000)
#define GRILIO_SUB_LEN (4)

/* Requests are considered pending for no longer than pending_timeout
 * because pending requests prevent blocking requests from being
 * submitted and we don't want to get stuck forever. */
#define GRILIO_DEFAULT_PENDING_TIMEOUT_MS (30000)

/* Miliseconds to microseconds */
#define MICROSEC(ms) (((gint64)(ms)) * 1000)

/* Log module */
GLOG_MODULE_DEFINE("grilio");

/* Object definition */
struct grilio_channel_priv {
    char* name;
    GIOChannel* io_channel;
    guint read_watch_id;
    guint write_watch_id;
    guint last_req_id;
    guint last_logger_id;
    GHashTable* req_table;
    GHashTable* pending;
    int pending_timeout;
    guint pending_timeout_id;
    gint64 next_pending_deadline;
    GSList* log_list;

    /* Serialization */
    guint last_block_id;
    GHashTable* block_ids;
    GRilIoRequest* block_req;
    GRilIoQueue* owner;
    GSList* owner_queue;

    /* Timeouts */
    int timeout;
    guint timeout_id;
    gint64 next_deadline;

    /* Subscription */
    gchar sub[GRILIO_SUB_LEN];
    guint sub_pos;

    /* Retry queue (sorted) */
    GRilIoRequest* retry_req;

    /* Send queue */
    GRilIoRequest* first_req;
    GRilIoRequest* last_req;

    /* Send */
    guint send_pos;
    GRilIoRequest* send_req;

    /* Receive */
    gchar read_len_buf[4];
    guint read_len_pos;
    guint read_len;
    guint read_buf_pos;
    guint read_buf_alloc;
    gchar* read_buf;
};

typedef GObjectClass GRilIoChannelClass;
G_DEFINE_TYPE(GRilIoChannel, grilio_channel, G_TYPE_OBJECT)
#define GRILIO_CHANNEL_TYPE (grilio_channel_get_type())
#define GRILIO_CHANNEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        GRILIO_CHANNEL_TYPE, GRilIoChannel))

enum grilio_channel_signal {
    SIGNAL_CONNECTED,
    SIGNAL_UNSOL_EVENT,
    SIGNAL_ERROR,
    SIGNAL_EOF,
    SIGNAL_COUNT
};

#define SIGNAL_CONNECTED_NAME   "grilio-connected"
#define SIGNAL_UNSOL_EVENT_NAME "grilio-unsol-event"
#define SIGNAL_ERROR_NAME       "grilio-error"
#define SIGNAL_EOF_NAME         "grilio-eof"

#define SIGNAL_UNSOL_EVENT_DETAIL_FORMAT        "%x"
#define SIGNAL_UNSOL_EVENT_DETAIL_MAX_LENGTH    (8)

static guint grilio_channel_signals[SIGNAL_COUNT] = { 0 };

typedef struct grilio_channel_logger {
    int id;
    GrilIoChannelLogFunc log;
    void* user_data;
} GrilIoChannelLogger;

typedef enum grilio_channel_error_type {
    GRILIO_ERROR_READ,
    GRILIO_ERROR_WRITE
} GRILIO_ERROR_TYPE;

static
gboolean
grilio_channel_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data);

static
void
grilio_channel_reset_timeout(
    GRilIoChannel* self);

static
void
grilio_channel_reset_pending_timeout(
    GRilIoChannel* self);

static
void
grilio_channel_schedule_write(
    GRilIoChannel* self);

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
grilio_channel_log(
    GRilIoChannel* self,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    gsize data_len)
{
    GRilIoChannelPriv* priv = self->priv;
    GSList* link = priv->log_list;
    while (link) {
        GSList* next = link->next;
        GrilIoChannelLogger* logger = link->data;
        logger->log(self, type, id, code, data, data_len, logger->user_data);
        link = next;
    }
}

static
guint
grilio_channel_generate_id(
    GHashTable* table,
    guint* id)
{
    (*id)++;
    while (!(*id) || g_hash_table_contains(table, GINT_TO_POINTER((*id)))) {
        (*id)++;
    }
    return (*id);
}

static
guint
grilio_channel_generate_req_id(
    GRilIoChannelPriv* priv)
{
    return grilio_channel_generate_id(priv->req_table, &priv->last_req_id);
}

static
guint
grilio_channel_generate_block_id(
    GRilIoChannelPriv* priv)
{
    return grilio_channel_generate_id(priv->block_ids, &priv->last_block_id);
}

static
gboolean
grilio_channel_serialized(
    GRilIoChannelPriv* priv)
{
    return priv->block_ids && g_hash_table_size(priv->block_ids);
}

static
void
grilio_channel_queue_request(
    GRilIoChannelPriv* queue,
    GRilIoRequest* req)
{
    GASSERT(!req->next);
    GASSERT(req->status == GRILIO_REQUEST_NEW ||
            req->status == GRILIO_REQUEST_RETRY);
    req->status = GRILIO_REQUEST_QUEUED;
    if (queue->last_req) {
        queue->last_req->next = req;
        queue->last_req = req;
    } else {
        GASSERT(!queue->first_req);
        queue->first_req = queue->last_req = req;
    }
    GVERBOSE("Queued request %08x (%08x)", req->id, req->current_id);
}

static
void
grilio_channel_requeue_request(
    GRilIoChannel* self,
    GRilIoRequest* req)
{
    GRilIoChannelPriv* priv = self->priv;

    GASSERT(!g_hash_table_contains(priv->req_table,
        GINT_TO_POINTER(req->id)));
    GASSERT(!g_hash_table_contains(priv->req_table,
        GINT_TO_POINTER(req->current_id)));
    GASSERT(!g_hash_table_contains(priv->pending,
        GINT_TO_POINTER(req->current_id)));

    /* Generate new request id. The first one is kept around because
     * it was returned to the caller. */
    req->current_id = grilio_channel_generate_req_id(priv);
    GASSERT(req->id != req->current_id);

    req->deadline = 0;
    req->retry_count++;

    /* Stick both public and private ids into the table (for cancel) */
    g_hash_table_insert(priv->req_table,
        GINT_TO_POINTER(req->id),
        grilio_request_ref(req));
    g_hash_table_insert(priv->req_table,
        GINT_TO_POINTER(req->current_id),
        grilio_request_ref(req));

    GVERBOSE("Queued retry #%d for request %08x", req->retry_count, req->id);
    grilio_channel_queue_request(priv, req);
    grilio_channel_schedule_write(self);
}

static
GRilIoRequest*
grilio_channel_dequeue_request(
    GRilIoChannelPriv* priv)
{
    GRilIoRequest* prev = NULL;
    GRilIoRequest* req = priv->first_req;
    if (req) {
        /* Handle the special (serialized) cases */
        if (priv->owner) {
            if (g_hash_table_size(priv->pending)) {
                GHashTableIter iter;
                gpointer key, value;
                g_hash_table_iter_init(&iter, priv->pending);
                while (g_hash_table_iter_next(&iter, &key, &value)) {
                    GRilIoRequest* pending = value;
                    if (pending->queue != priv->owner) {
                        /* This request is not associated with the queue
                         * which owns the channel. Wait until all such
                         * requests complete, before we start submiting
                         * requests associated with the owner queue. */
                        req = NULL;
                        break;
                    }
                }
            }
            /* If a transaction is in progress, pick the first request
             * that belongs to the transaction */
            while (req && req->queue != priv->owner) {
                prev = req;
                req = req->next;
            }
        } else if (grilio_channel_serialized(priv) || req->blocking) {
            if (g_hash_table_size(priv->pending)) {
                /* We need to wait for the currently pending request(s)
                 * to complete or time out before we can submit a blocking
                 * request. */
                req = NULL;
            }
        }
    }
    if (req) {
        if (prev) {
            prev->next = req->next;
        } else {
            priv->first_req = req->next;
        }
        if (req->next) {
            req->next = NULL;
            GASSERT(priv->last_req);
            GASSERT(priv->last_req != req);
        } else {
            GASSERT(priv->last_req == req);
            priv->last_req = prev;
        }
        GASSERT(req->status == GRILIO_REQUEST_QUEUED);
        req->status = GRILIO_REQUEST_SENDING;
        GVERBOSE("Sending request %08x (%08x)", req->id, req->current_id);
    }
    return req;
}

static
void
grilio_channel_drop_request(
    GRilIoChannelPriv* priv,
    GRilIoRequest* req)
{
    grilio_queue_remove(req);
    g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->current_id));
    if (req->id != req->current_id) {
        g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->id));
    }
}

static
void
grilio_channel_handle_error(
    GRilIoChannel* self,
    GRILIO_ERROR_TYPE type,
    GError* error)
{
    GERR("%s %s failed: %s", self->name, (type == GRILIO_ERROR_READ) ?
         "read" : "write", error->message);
    /* Zero watch ids because we're going to return FALSE from the callback */
    if (type == GRILIO_ERROR_READ) {
        self->priv->read_watch_id = 0;
    } else {
        GASSERT(type == GRILIO_ERROR_WRITE);
        self->priv->write_watch_id = 0;
    }
    grilio_channel_shutdown(self, FALSE);
    g_signal_emit(self, grilio_channel_signals[SIGNAL_ERROR], 0, error);
    g_error_free(error);
}

static
void
grilio_channel_handle_eof(
    GRilIoChannel* self)
{
    GERR("%s hangup", self->name);
    grilio_channel_shutdown(self, FALSE);
    g_signal_emit(self, grilio_channel_signals[SIGNAL_EOF], 0);
}

static
void
grilio_channel_schedule_retry(
    GRilIoChannelPriv* priv,
    GRilIoRequest* req)
{
    GASSERT(!req->next);
    req->deadline = g_get_monotonic_time() + MICROSEC(req->retry_period);
    req->status = GRILIO_REQUEST_RETRY;

    /* Remove the request from the request table while it's waiting
     * for its turn to retry. It means that it gets completed during
     * this time, we will miss the reply. */
    grilio_request_ref(req);
    g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->current_id));
    if (req->id != req->current_id) {
        g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->id));
    }

    GVERBOSE("Retry #%d for request %08x in %u ms", req->retry_count+1,
        req->id, req->retry_period);

    /* Keep the retry queue sorted by deadline */
    if (priv->retry_req && priv->retry_req->deadline < req->deadline) {
        GRilIoRequest* prev = priv->retry_req;
        GRilIoRequest* next = prev->next;
        while (next && next->deadline < req->deadline) {
            prev = next;
            next = prev->next;
        }
        prev->next = req;
        req->next = next;
    } else {
        req->next = priv->retry_req;
        priv->retry_req = req;
    }
}

static
gint64
grilio_channel_pending_deadline(
    GRilIoChannelPriv* priv,
    GRilIoRequest* req)
{
    const int req_timeout = (req->timeout > 0 &&
        req->timeout <= priv->pending_timeout) ?
        req->timeout : priv->pending_timeout;
    GASSERT(req->submitted);
    return req->submitted + MICROSEC(req_timeout);
}

static
gboolean
grilio_channel_pending_timeout(
    gpointer user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRilIoChannelPriv* priv = self->priv;
    const gint64 now = g_get_monotonic_time();
    GHashTableIter iter;
    gpointer value;

    priv->pending_timeout_id = 0;
    g_hash_table_iter_init(&iter, priv->pending);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        GRilIoRequest* req = value;
        const gint64 t = grilio_channel_pending_deadline(priv, req);
        if (t <= now) {
            GDEBUG("Pending request %08x (%08x) expired", req->id,
                req->current_id);
            if (req == priv->block_req) {
                /* Let the life continue */
                grilio_request_unref(priv->block_req);
                priv->block_req = NULL;
            }
            g_hash_table_iter_remove(&iter);
        }
    }

    grilio_channel_reset_pending_timeout(self);
    grilio_channel_schedule_write(self);
    return G_SOURCE_REMOVE;
}

static
void
grilio_channel_reset_pending_timeout(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    if (g_hash_table_size(priv->pending)) {
        GHashTableIter iter;
        const gint64 now = g_get_monotonic_time();
        gint64 deadline = 0;
        gpointer value;

        /* Calculate the new deadline */
        g_hash_table_iter_init(&iter, priv->pending);
        while (g_hash_table_iter_next(&iter, NULL, &value)) {
            GRilIoRequest* req = value;
            const gint64 t = grilio_channel_pending_deadline(priv, req);
            if (!deadline || deadline > t) {
                deadline = t;
            }
        }

        if (priv->next_pending_deadline != deadline) {
           /* Reset the timer */
            if (priv->pending_timeout_id) {
                g_source_remove(priv->pending_timeout_id);
            }
            priv->next_pending_deadline = deadline;
            if (deadline > now) {
                priv->pending_timeout_id =
                    g_timeout_add(((deadline - now) + 999)/1000,
                        grilio_channel_pending_timeout, self);
            } else {
                priv->pending_timeout_id =
                    g_idle_add(grilio_channel_pending_timeout, self);
            }
        }
    } else {
        /* No more pending requests, cancel the timeout */
        if (priv->pending_timeout_id) {
            g_source_remove(priv->pending_timeout_id);
            priv->pending_timeout_id = 0;
        }
    }
}

static
gboolean
grilio_channel_timeout(
    gpointer user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRilIoChannelPriv* priv = self->priv;
    GRilIoRequest* expired = NULL;
    const gint64 now = g_get_monotonic_time();
    gboolean pending_expired = FALSE;
    GHashTableIter iter;
    gpointer key, value;

    priv->timeout_id = 0;
    priv->next_deadline = 0;

    /* Expired requests */
    g_hash_table_iter_init(&iter, priv->req_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        GRilIoRequest* req = value;
        if (key == GINT_TO_POINTER(req->current_id) &&
            req->deadline && req->deadline <= now) {
            GASSERT(!req->next);
            req->next = expired;
            req->deadline = 0;
            GDEBUG("%s %08x (%08x) timed out", (priv->block_req == req) ?
                "Blocking request" : "Request", req->id, req->current_id);
            if (priv->block_req == req) {
                expired = priv->block_req;
                priv->block_req = NULL;
            } else {
                expired = grilio_request_ref(req);
            }
        }
    }

    while (expired) {
        GRilIoRequest* req = expired;
        expired = req->next;
        req->next = NULL;
        if (g_hash_table_remove(priv->pending,
            GINT_TO_POINTER(req->current_id))) {
            pending_expired = TRUE;
        }
        g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->id));
        if (grilio_request_can_retry(req)) {
            grilio_channel_schedule_retry(priv, req);
        } else {
            grilio_channel_drop_request(priv, req);
            req->status = GRILIO_REQUEST_DONE;
            if (req->response) {
                req->response(self, GRILIO_STATUS_TIMEOUT, NULL, 0,
                    req->user_data);
            }
        }
        grilio_request_unref(req);
    }

    /* Expired retry timeouts */
    if (priv->retry_req && priv->retry_req->deadline <= now) {
        /* Slip expired timeout to preserve the order */
        GRilIoRequest* last = priv->retry_req;
        while (last->next && last->next->deadline <= now) {
            last = last->next;
        }
        expired = priv->retry_req;
        priv->retry_req = last->next;
        last->next = NULL;
    }

    while (expired) {
        GRilIoRequest* req = expired;
        expired = req->next;
        req->next = NULL;
        grilio_channel_requeue_request(self, req);
    }

    if (pending_expired) {
        grilio_channel_reset_pending_timeout(self);
    }
    grilio_channel_reset_timeout(self);
    grilio_channel_schedule_write(self);
    return G_SOURCE_REMOVE;
}

static
void
grilio_channel_reset_timeout(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    GHashTableIter iter;
    const gint64 now = g_get_monotonic_time();
    gint64 deadline = 0;
    gpointer value;

    if (priv->block_req && priv->block_req->deadline) {
        deadline = priv->block_req->deadline;
    }

    /* This loop shouldn't impact the performance because the hash table
     * typically contains very few entries */
    g_hash_table_iter_init(&iter, priv->req_table);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        GRilIoRequest* req = value;
        if (req->deadline) {
            if (!deadline || deadline > req->deadline) {
                deadline = req->deadline;
            }
        }
    }

    /* Retry requests are sorted */
    if (priv->retry_req) {
        if (!deadline || deadline > priv->retry_req->deadline) {
            deadline = priv->retry_req->deadline;
        }
    }

    if (deadline) {
        if (!priv->next_deadline || priv->next_deadline > deadline) {
            if (priv->timeout_id) {
                g_source_remove(priv->timeout_id);
            }
            priv->timeout_id = (deadline <= now) ?
                g_idle_add(grilio_channel_timeout, self) :
                g_timeout_add(((deadline - now) + 999)/1000,
                    grilio_channel_timeout, self);
            if (priv->timeout_id) {
                priv->next_deadline = deadline;
            }
        }
    } else if (priv->timeout_id) {
        g_source_remove(priv->timeout_id);
        priv->timeout_id = 0;
        priv->next_deadline = 0;
    }
}

static
gboolean
grilio_channel_write(
    GRilIoChannel* self)
{
    GError* error = NULL;
    gsize bytes_written;
    int req_timeout;
    GRilIoRequest* req;
    GRilIoChannelPriv* priv = self->priv;

    if (priv->sub_pos < GRILIO_SUB_LEN) {
        bytes_written = 0;
        g_io_channel_write_chars(priv->io_channel, priv->sub + priv->sub_pos,
            GRILIO_SUB_LEN - priv->sub_pos, &bytes_written, &error);
        if (error) {
            grilio_channel_handle_error(self, GRILIO_ERROR_WRITE, error);
            return FALSE;
        }
        priv->sub_pos += bytes_written;
        GASSERT(priv->sub_pos <= GRILIO_SUB_LEN);
        if (priv->sub_pos < GRILIO_SUB_LEN) {
            /* Will have to wait */
            return TRUE;
        }
        GDEBUG("%s subscribed for %c%c%c%c", self->name,
            priv->sub[0], priv->sub[1], priv->sub[2], priv->sub[3]);
    }

    if (!self->connected) {
        GVERBOSE("%s not connected yet", self->name);
        return FALSE;
    }

    if (priv->block_req) {
        GVERBOSE("%s waiting for request %08x to complete", self->name,
            priv->block_req->current_id);
        return FALSE;
    }

    req = priv->send_req;
    if (!req) {
        req = priv->send_req = grilio_channel_dequeue_request(priv);
        if (req) {
            /* Prepare the next request for sending */
            guint32* header = (guint32*)req->bytes->data;
            header[0] = GINT32_TO_BE(req->bytes->len - 4);
            header[1] = GUINT32_TO_RIL(req->code);
            header[2] = GUINT32_TO_RIL(req->current_id);
            priv->send_pos = 0;
            /* Keep track of pending requests */
            req->submitted = g_get_monotonic_time();
            g_hash_table_insert(priv->pending,
                GINT_TO_POINTER(req->current_id),
                grilio_request_ref(req));
            grilio_channel_reset_pending_timeout(self);
        } else {
            /* There is nothing to send, remove the watch */
            GVERBOSE("%s has nothing to send", self->name);
            return FALSE;
        }
    }

    req_timeout = req->timeout;
    if (req_timeout == GRILIO_TIMEOUT_DEFAULT && priv->timeout > 0) {
        req_timeout = priv->timeout;
    }

    if (grilio_channel_serialized(priv) || req->blocking) {
        /* Block next requests from being sent while this one is pending */
        priv->block_req = grilio_request_ref(req);
    }

    if (priv->send_pos < req->bytes->len) {
        bytes_written = 0;
        g_io_channel_write_chars(priv->io_channel,
            (void*)(req->bytes->data + priv->send_pos),
            req->bytes->len - priv->send_pos,
            &bytes_written, &error);
        if (error) {
            grilio_channel_handle_error(self, GRILIO_ERROR_WRITE, error);
            return FALSE;
        }
        priv->send_pos += bytes_written;
        GASSERT(priv->send_pos <= req->bytes->len);
        if (priv->send_pos < req->bytes->len) {
            /* Will have to wait */
            return TRUE;
        }
    }

    /* The request has been sent */
    if (req->status == GRILIO_REQUEST_SENDING) {
        req->status = GRILIO_REQUEST_SENT;
    } else {
        GASSERT(req->status == GRILIO_REQUEST_CANCELLED);
    }

    grilio_channel_log(self, GRILIO_PACKET_REQ, req->current_id, req->code,
        req->bytes->data + 4, req->bytes->len - 4);

    /* If there's no response callback and no need to retry, remove it
     * from the queue as well */
    if (!req->response && !grilio_request_can_retry(req)) {
        grilio_queue_remove(req);
        if (!priv->block_req) {
            req_timeout = 0;
        }
    }

    if (req_timeout > 0) {
        /* This request has a timeout */
        req->deadline = g_get_monotonic_time() + MICROSEC(req_timeout);
        if (!priv->next_deadline || req->deadline < priv->next_deadline) {
            grilio_channel_reset_timeout(self);
        }
    }

    grilio_request_unref(req);
    priv->send_req = NULL;

    /* Remove the watch if there's no more requests */
    if (priv->first_req) {
        return TRUE;
    } else {
        GVERBOSE("%s queue empty", self->name);
        return FALSE;
    }
}

gboolean
grilio_channel_retry_request(
    GRilIoChannel* self,
    guint id)
{
    if (G_LIKELY(self && id)) {
        GRilIoChannelPriv* priv = self->priv;
        GRilIoRequest* prev = NULL;
        GRilIoRequest* req;

        if (priv->block_req && priv->block_req->id == id) {
            /* Already sent but not yet replied to */
            GVERBOSE("Request %08x is pending", id);
            return FALSE;
        }

        /* This queue is typically empty or quite small */
        for (req = priv->first_req; req; req = req->next) {
            if (req->id == id) {
                /* Already queued */
                GVERBOSE("Request %08x is already queued", id);
                return TRUE;
            }
        }

        /* Requests sitting in the retry queue must not be in the table */
        if (g_hash_table_lookup(priv->req_table, GINT_TO_POINTER(id))) {
            /* Just been sent, no reply yet */
            GVERBOSE("Request %08x is in progress", id);
            return FALSE;
        }

        /* Check the retry queue then */
        for (req = priv->retry_req; req; req = req->next) {
            if (req->id == id) {
                GDEBUG("Retrying request %08x", id);
                if (prev) {
                    prev->next = req->next;
                } else {
                    priv->retry_req = req->next;
                }
                req->next = NULL;
                grilio_channel_requeue_request(self, req);
                grilio_channel_reset_timeout(self);
                return TRUE;
            }
            prev = req;
        }

        /* Probably an invalid request id */
        GWARN("Can't retry request %08x", id);
    }
    return FALSE;
}

static
void
grilio_channel_schedule_write(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    if (self->connected && priv->io_channel && !priv->write_watch_id) {
        /* grilio_channel_write() will return FALSE if everything has been
         * written. In that case there's no need to create G_IO_OUT watch. */
        if (grilio_channel_write(self)) {
            GVERBOSE("%s scheduling write", self->name);
            priv->write_watch_id = g_io_add_watch(priv->io_channel, G_IO_OUT,
                grilio_channel_write_callback, self);
        }
    }
}

static
void
grilio_channel_connected(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    GASSERT(!self->connected);
    if (priv->read_len > RIL_UNSOL_HEADER_SIZE) {
        GRilIoParser parser;
        guint num = 0;
        grilio_parser_init(&parser, priv->read_buf + RIL_UNSOL_HEADER_SIZE,
            priv->read_len - RIL_UNSOL_HEADER_SIZE);
        if (grilio_parser_get_uint32(&parser, &num) && num == 1 &&
            grilio_parser_get_uint32(&parser, &self->ril_version)) {
            GDEBUG("Connected, RIL version %u", self->ril_version);
            self->connected = TRUE;
            g_signal_emit(self, grilio_channel_signals[SIGNAL_CONNECTED], 0);
            grilio_channel_schedule_write(self);
        } else {
            GERR("Failed to parse RIL_UNSOL_RIL_CONNECTED");
        }
    }
}

static
gboolean
grilio_channel_handle_packet(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    if (priv->read_len >= RIL_MIN_HEADER_SIZE) {
        const guint32* buf = (guint32*)priv->read_buf;
        if (buf[0]) {
            /* RIL Unsolicited Event */
            const guint32 code = GUINT32_FROM_RIL(buf[1]);

            /* Event code is the detail */
            GQuark detail;
            char buf[SIGNAL_UNSOL_EVENT_DETAIL_MAX_LENGTH + 1];
            snprintf(buf, sizeof(buf), SIGNAL_UNSOL_EVENT_DETAIL_FORMAT, code);
            detail = g_quark_from_string(buf);

            /* Logger get the whole thing except the length */
            grilio_channel_log(self, GRILIO_PACKET_UNSOL, 0, code,
                priv->read_buf, priv->read_len);

            /* Handle RIL_UNSOL_RIL_CONNECTED */
            if (code == RIL_UNSOL_RIL_CONNECTED) {
                grilio_channel_connected(self);
            }

            /* Event handler gets event code and the data separately */
            g_signal_emit(self, grilio_channel_signals[SIGNAL_UNSOL_EVENT],
                detail, code, priv->read_buf + RIL_UNSOL_HEADER_SIZE,
                priv->read_len - RIL_UNSOL_HEADER_SIZE);
            return TRUE;
        } else if (priv->read_len >= RIL_RESPONSE_HEADER_SIZE) {
            /* RIL Solicited Response */
            const guint32 id = GUINT32_FROM_RIL(buf[1]);
            const guint32 status = GUINT32_FROM_RIL(buf[2]);
            GRilIoRequest* req = g_hash_table_lookup(priv->req_table,
                GINT_TO_POINTER(id));

            /* Remove this id from the list of pending requests */
            if (g_hash_table_remove(priv->pending, GINT_TO_POINTER(id))) {
                /* Reset submit time */
                if (req) req->submitted = 0;
                grilio_channel_reset_pending_timeout(self);
            }

            /* Logger receives everything except the length */
            grilio_channel_log(self, GRILIO_PACKET_RESP, id, status,
                priv->read_buf, priv->read_len);

            if (priv->block_req && priv->block_req->current_id == id) {
                /* Blocking request has completed */
                grilio_request_unref(priv->block_req);
                priv->block_req = NULL;
            }

            /* Handle the case if we receive a response with the id of the
             * packet which we haven't sent yet. */
            if (req && req->status == GRILIO_REQUEST_SENT) {
                const void* resp = priv->read_buf + RIL_RESPONSE_HEADER_SIZE;
                const guint len = priv->read_len - RIL_RESPONSE_HEADER_SIZE;

                GASSERT(req->current_id == id);
                /* Temporary increment the ref count to compensate for
                 * g_hash_table_remove possibly unreferencing the request */
                grilio_request_ref(req);
                if (grilio_request_can_retry(req) &&
                    req->retry(req, status, resp, len, req->user_data)) {
                    /* Will retry, keep it around */
                    grilio_channel_schedule_retry(priv, req);
                    grilio_channel_reset_timeout(self);
                } else {
                    grilio_channel_drop_request(priv, req);
                    req->status = GRILIO_REQUEST_DONE;
                    if (req->response) {
                        req->response(self, status, resp, len, req->user_data);
                    }
                }

                /* Release temporary reference */
                grilio_request_unref(req);
            }

            /* Completed request may unblock the writes */
            grilio_channel_schedule_write(self);
            return TRUE;
        }
    }

    grilio_channel_handle_error(self, GRILIO_ERROR_READ,
        g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "Packet too short (%u bytes)", priv->read_len));
    return FALSE;
}

static
gboolean
grilio_channel_read_chars(
    GRilIoChannel* self,
    gchar* buf,
    gsize count,
    gsize* bytes_read)
{
    GError* error = NULL;
    GIOStatus status = g_io_channel_read_chars(self->priv->io_channel, buf,
        count, bytes_read, &error);
    if (error) {
        grilio_channel_handle_error(self, GRILIO_ERROR_READ, error);
        return FALSE;
    } else if (status == G_IO_STATUS_EOF) {
        grilio_channel_handle_eof(self);
        return FALSE;
    } else {
        return TRUE;
    }
}

static
gboolean
grilio_channel_read(
    GRilIoChannel* self)
{
    gsize bytes_read;
    GRilIoChannelPriv* priv = self->priv;

    /* Length */
    if (priv->read_len_pos < 4) {
        if (!grilio_channel_read_chars(self,
            priv->read_len_buf + priv->read_len_pos,
            4 - priv->read_len_pos, &bytes_read)) {
            return FALSE;
        }
        priv->read_len_pos += bytes_read;
        GASSERT(priv->read_len_pos <= 4);
        if (priv->read_len_pos < 4) {
            /* Need more bytes */
            return TRUE;
        } else {
            /* We have finished reading the length (in Big Endian) */
            const guint32* len = (guint32*)priv->read_len_buf;
            priv->read_len = GUINT32_FROM_BE(*len);
            GASSERT(priv->read_len <= GRILIO_MAX_PACKET_LEN);
            if (priv->read_len <= GRILIO_MAX_PACKET_LEN) {
                /* Reset buffer read position */
                priv->read_buf_pos = 0;
                /* Allocate enough space for the entire packet */
                if (priv->read_buf_alloc < priv->read_len) {
                    g_free(priv->read_buf);
                    priv->read_buf_alloc = priv->read_len;
                    priv->read_buf = g_malloc(priv->read_buf_alloc);
                }
            } else {
                /* Message is too long or stream is broken */
                return FALSE;
            }
        }
    }

    /* Packet body */
    if (priv->read_buf_pos < priv->read_len) {
        if (!grilio_channel_read_chars(self,
            priv->read_buf + priv->read_buf_pos,
            priv->read_len - priv->read_buf_pos, &bytes_read)) {
            return FALSE;
        }
        priv->read_buf_pos += bytes_read;
        GASSERT(priv->read_buf_pos <= priv->read_len);
        if (priv->read_buf_pos < priv->read_len) {
            /* Need more bytes */
            return TRUE;
        }
    }

    /* Reset the reading position to indicate that we are ready to start
     * receiving the next packet */
    priv->read_len_pos = 0;

    /* We have finished reading the entire packet */
    return grilio_channel_handle_packet(self);
}

static
gboolean
grilio_channel_read_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(data);
    gboolean ok;
    grilio_channel_ref(self);
    ok = (condition & G_IO_IN) && grilio_channel_read(self);
    if (!ok) self->priv->read_watch_id = 0;
    grilio_channel_unref(self);
    return ok;
}

static
gboolean
grilio_channel_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(data);
    gboolean ok;
    grilio_channel_ref(self);
    ok = (condition & G_IO_OUT) && grilio_channel_write(self);
    if (!ok) self->priv->write_watch_id = 0;
    grilio_channel_unref(self);
    return ok;
}

/*==========================================================================*
 * API
 *==========================================================================*/

GRilIoChannel*
grilio_channel_new_socket(
    const char* path,
    const char* sub)
{
    if (G_LIKELY(path)) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path));
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                GRilIoChannel* channel = grilio_channel_new_fd(fd, sub, TRUE);
                if (channel) {
                    GDEBUG("Opened %s", path);
                    return channel;
                }
            } else {
		GERR("Can't connect to RILD: %s", strerror(errno));
            }
            close(fd);
	}
    } else {
        GERR("Can't create unix socket: %s", strerror(errno));
    }
    return NULL;
}

GRilIoChannel*
grilio_channel_new_fd(
    int fd,
    const char* sub,
    gboolean can_close)
{
    if (G_LIKELY(fd >= 0 && (!sub || strlen(sub) == GRILIO_SUB_LEN))) {
        GRilIoChannelPriv* priv;
        GRilIoChannel* chan = g_object_new(GRILIO_CHANNEL_TYPE, NULL);
        priv = chan->priv;
        priv->io_channel = g_io_channel_unix_new(fd);
        if (priv->io_channel) {
            g_io_channel_set_flags(priv->io_channel, G_IO_FLAG_NONBLOCK, NULL);
            g_io_channel_set_encoding(priv->io_channel, NULL, NULL);
            g_io_channel_set_buffered(priv->io_channel, FALSE);
            g_io_channel_set_close_on_unref(priv->io_channel, can_close);
            priv->read_watch_id = g_io_add_watch(priv->io_channel,
                G_IO_IN, grilio_channel_read_callback, chan);
            if (sub) {
                memcpy(priv->sub, sub, GRILIO_SUB_LEN);
                priv->write_watch_id = g_io_add_watch(priv->io_channel,
                    G_IO_OUT, grilio_channel_write_callback, chan);
            } else {
                priv->sub_pos = GRILIO_SUB_LEN;
            }
            return chan;
        }
        grilio_channel_unref(chan);
    }
    return NULL;
}

void
grilio_channel_shutdown(
    GRilIoChannel* self,
    gboolean flush)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        if (priv->read_watch_id) {
            g_source_remove(priv->read_watch_id);
            priv->read_watch_id = 0;
        }
        if (priv->write_watch_id) {
            g_source_remove(priv->write_watch_id);
            priv->write_watch_id = 0;
        }
        if (priv->io_channel) {
            g_io_channel_shutdown(priv->io_channel, flush, NULL);
            g_io_channel_unref(priv->io_channel);
            priv->io_channel = NULL;
        }
        self->connected = FALSE;
        self->ril_version = 0;
    }
}

GRilIoChannel*
grilio_channel_ref(
    GRilIoChannel* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GRILIO_CHANNEL(self));
        return self;
    } else {
        return NULL;
    }
}

void
grilio_channel_unref(
    GRilIoChannel* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GRILIO_CHANNEL(self));
    }
}

void
grilio_channel_set_timeout(
    GRilIoChannel* self,
    int timeout)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        if (timeout == GRILIO_TIMEOUT_DEFAULT) {
            timeout = GRILIO_TIMEOUT_NONE;
        }
        /* NOTE: this doesn't affect requests that have already been sent */
        priv->timeout = timeout;
    }
}

void
grilio_channel_set_name(
    GRilIoChannel* self,
    const char* name)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        g_free(priv->name);
        self->name = priv->name = g_strdup(name);
    }
}

guint
grilio_channel_serialize(
    GRilIoChannel* self)
{
    guint id = 0;
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        if (!priv->block_ids) {
            GDEBUG("Serializing %s", self->name);
            priv->block_ids = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        id = grilio_channel_generate_block_id(priv);
        g_hash_table_insert(priv->block_ids, GINT_TO_POINTER(id),
            GINT_TO_POINTER(id));
    }
    return id;
}

void
grilio_channel_deserialize(
    GRilIoChannel* self,
    guint id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        GRilIoChannelPriv* priv = self->priv;
        if (grilio_channel_serialized(priv)) {
            g_hash_table_remove(priv->block_ids, GINT_TO_POINTER(id));
            if (!grilio_channel_serialized(priv)) {
                GDEBUG("Deserializing %s", self->name);
                if (priv->block_req && !priv->block_req->blocking) {
                    grilio_request_unref(priv->block_req);
                    priv->block_req = NULL;
                }
                grilio_channel_schedule_write(self);
            }
        }
    }
}

gulong
grilio_channel_add_connected_handler(
    GRilIoChannel* self,
    GRilIoChannelEventFunc func,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_CONNECTED_NAME, G_CALLBACK(func), arg) : 0;
}

gulong
grilio_channel_add_disconnected_handler(
    GRilIoChannel* self,
    GRilIoChannelEventFunc func,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_EOF_NAME, G_CALLBACK(func), arg) : 0;
}

gulong
grilio_channel_add_unsol_event_handler(
    GRilIoChannel* self,
    GRilIoChannelUnsolEventFunc func,
    guint code,
    void* arg)
{
    if (G_LIKELY(self) && G_LIKELY(func)) {
        const char* signal_name;
        char buf[sizeof(SIGNAL_UNSOL_EVENT_NAME) + 2 +
            SIGNAL_UNSOL_EVENT_DETAIL_MAX_LENGTH];
        if (code) {
            snprintf(buf, sizeof(buf), "%s::" SIGNAL_UNSOL_EVENT_DETAIL_FORMAT,
                SIGNAL_UNSOL_EVENT_NAME, code);
            buf[sizeof(buf)-1] = 0;
            signal_name = buf;
        } else {
            signal_name = SIGNAL_UNSOL_EVENT_NAME;
        }
        return g_signal_connect(self, signal_name, G_CALLBACK(func), arg);
    } else {
        return 0;
    }
}

gulong
grilio_channel_add_error_handler(
    GRilIoChannel* self,
    GRilIoChannelErrorFunc func,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_ERROR_NAME, G_CALLBACK(func), arg) : 0;
}

void
grilio_channel_remove_handler(
    GRilIoChannel* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
grilio_channel_remove_handlers(
    GRilIoChannel* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

guint
grilio_channel_add_logger(
    GRilIoChannel* self,
    GrilIoChannelLogFunc log,
    void* user_data)
{
    if (G_LIKELY(self && log)) {
        GRilIoChannelPriv* priv = self->priv;
        GrilIoChannelLogger* logger = g_slice_new(GrilIoChannelLogger);
        priv->last_logger_id++;
        if (!priv->last_logger_id) priv->last_logger_id++;
        logger->id = priv->last_logger_id;
        logger->log = log;
        logger->user_data = user_data;
        priv->log_list = g_slist_append(priv->log_list, logger);
        return logger->id;
    } else {
        return 0;
    }
}

void
grilio_channel_remove_logger(
    GRilIoChannel* self,
    guint id)
{
    if (G_LIKELY(self && id)) {
        GRilIoChannelPriv* priv = self->priv;
        GSList* link = priv->log_list;
        while (link) {
            GSList* next = link->next;
            GrilIoChannelLogger* logger = link->data;
            if (logger->id == id) {
                g_slice_free(GrilIoChannelLogger, logger);
                priv->log_list = g_slist_delete_link(priv->log_list, link);
                return;
            }
            link = next;
        }
        GWARN("Invalid logger id %u", id);
    }
}

guint
grilio_channel_send_request(
    GRilIoChannel* self,
    GRilIoRequest* req,
    guint code)
{
    return grilio_channel_send_request_full(self, req, code, NULL, NULL, NULL);
}

guint
grilio_channel_send_request_full(
    GRilIoChannel* self,
    GRilIoRequest* req,
    guint code,
    GRilIoChannelResponseFunc response,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self && (!req || req->status == GRILIO_REQUEST_NEW))) {
        GRilIoChannelPriv* priv = self->priv;
        GRilIoRequest* internal_req = NULL;
        const guint id = grilio_channel_generate_req_id(priv);
        if (!req) req = internal_req = grilio_request_new();
        req->id = req->current_id = id;
        req->code = code;
        req->response = response;
        req->destroy = destroy;
        req->user_data = user_data;
        g_hash_table_insert(priv->req_table,
            GINT_TO_POINTER(req->id),
            grilio_request_ref(req));
        grilio_channel_queue_request(priv, grilio_request_ref(req));
        grilio_channel_schedule_write(self);
        grilio_request_unref(internal_req);
        return id;
    }
    return 0;
}

void
grilio_channel_set_pending_timeout(
    GRilIoChannel* self,
    int ms)
{
    if (G_LIKELY(self) && ms > 0) {
        GRilIoChannelPriv* priv = self->priv;
        if (priv->pending_timeout < ms) {
            priv->pending_timeout = ms;
            grilio_channel_reset_pending_timeout(self);
        } else {
            priv->pending_timeout = ms;
        }
    }
}

GRILIO_TRANSACTION_STATE
grilio_channel_transaction_start(
    GRilIoChannel* self,
    GRilIoQueue* queue)
{
    GRILIO_TRANSACTION_STATE state = GRILIO_TRANSACTION_NONE;
    if (G_LIKELY(self && queue)) {
        GRilIoChannelPriv* priv = self->priv;
        if (!priv->owner) {
            priv->owner = queue;
            GASSERT(!priv->owner_queue);
            state = GRILIO_TRANSACTION_STARTED;
        } else if (priv->owner == queue) {
            /* Transaction is already in progress */
            state = GRILIO_TRANSACTION_STARTED;
        } else if (priv->owner_queue) {
            /* Avoid scanning the queue twice */
            GSList* l = priv->owner_queue;
            GSList* last = l;
            while (l) {
                if (l->data == queue) {
                    break;
                }
                last = l;
                l = l->next;
            }
            if (!l) {
                /* Not found in the queue */
                last->next = g_slist_append(NULL, queue);
            }
            state = GRILIO_TRANSACTION_QUEUED;
        } else {
            /* This is the first entry in the queue */
            priv->owner_queue = g_slist_append(NULL, queue);
            state = GRILIO_TRANSACTION_QUEUED;
        }
    }
    return state;
}

GRILIO_TRANSACTION_STATE
grilio_channel_transaction_state(
    GRilIoChannel* self,
    GRilIoQueue* queue)
{
    if (G_LIKELY(self && queue)) {
        GRilIoChannelPriv* priv = self->priv;
        if (priv->owner == queue) {
            return GRILIO_TRANSACTION_STARTED;
        } else if (g_slist_find(priv->owner_queue, queue)) {
            return GRILIO_TRANSACTION_QUEUED;
        }
    }
    return GRILIO_TRANSACTION_NONE;
}

void
grilio_channel_transaction_finish(
    GRilIoChannel* self,
    GRilIoQueue* queue)
{
    if (G_LIKELY(self && queue)) {
        GRilIoChannelPriv* priv = self->priv;
        if (priv->owner == queue) {
            if (priv->owner_queue) {
                priv->owner = priv->owner_queue->data;
                priv->owner_queue = g_slist_delete_link(priv->owner_queue,
                    priv->owner_queue);
            } else {
                priv->owner = NULL;
            }
            grilio_channel_schedule_write(self);
        } else {
            priv->owner_queue = g_slist_remove(priv->owner_queue, queue);
        }
    }
}

GRilIoRequest*
grilio_channel_get_request(
    GRilIoChannel* self,
    guint id)
{
    GRilIoRequest* req = NULL;
    if (G_LIKELY(self && id)) {
        GRilIoChannelPriv* priv = self->priv;
        if (priv->send_req && priv->send_req->id == id) {
            req = priv->send_req;
        } else if (priv->block_req && priv->block_req->id == id) {
            req = priv->block_req;
        } else {
            req = g_hash_table_lookup(priv->req_table, GINT_TO_POINTER(id));
            if (!req) {
                req = priv->first_req;
                while (req && req->id != id) req = req->next;
                if (!req) {
                    req = priv->retry_req;
                    while (req && req->id != id) req = req->next;
                }
            }
        }
    }
    return req;
}

gboolean
grilio_channel_cancel_request(
    GRilIoChannel* self,
    guint id,
    gboolean notify)
{
    if (G_LIKELY(self && id)) {
        GRilIoChannelPriv* priv = self->priv;
        GRilIoRequest* block_req = NULL;
        GRilIoRequest* req;

        if (priv->block_req && priv->block_req->id == id) {
            /* Don't release the reference or change the status yet. However
             * remember to drop the reference before we return. */
            block_req = priv->block_req;
            priv->block_req = NULL;
        }

        if (priv->send_req && priv->send_req->id == id) {
            /* Current request will be unreferenced after it's sent */
            req = priv->send_req;
            if (req->status != GRILIO_REQUEST_CANCELLED) {
                req->status = GRILIO_REQUEST_CANCELLED;
                grilio_channel_drop_request(priv, req);
                if (notify && req->response) {
                    req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                        req->user_data);
                }
                grilio_request_unref(block_req);
                grilio_channel_schedule_write(self);
                return TRUE;
            } else {
                grilio_request_unref(block_req);
                return FALSE;
            }
        } else {
            GRilIoRequest* prev = NULL;
            for (req = priv->first_req; req; req = req->next) {
                if (req->id == id) {
                    GDEBUG("Cancelled request %08x (%08x)", id,
                        req->current_id);
                    if (prev) {
                        prev->next = req->next;
                    } else {
                        priv->first_req = req->next;
                    }
                    if (req->next) {
                        req->next = NULL;
                    } else {
                        priv->last_req = prev;
                    }
                    grilio_channel_drop_request(priv, req);
                    req->status = GRILIO_REQUEST_CANCELLED;
                    if (notify && req->response) {
                        req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                            req->user_data);
                    }
                    grilio_request_unref(req);
                    grilio_request_unref(block_req);
                    grilio_channel_schedule_write(self);
                    return TRUE;
                }
                prev = req;
            }
        }

        /* Request not found but it could've been already sent and be setting
         * in the hashtable waiting for response */
        req = g_hash_table_lookup(priv->req_table, GINT_TO_POINTER(id));
        if (req) {
            /* We need this extra temporary reference because the hash table
             * may be holding the last one, i.e. removing request from 
             * hash table may deallocate the request */
            grilio_request_ref(req);
            grilio_channel_drop_request(priv, req);
            req->status = GRILIO_REQUEST_CANCELLED;
            if (notify && req->response) {
                req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                    req->user_data);
            }
            grilio_request_unref(req);
            grilio_request_unref(block_req);
            grilio_channel_reset_timeout(self);
            grilio_channel_schedule_write(self);
            return TRUE;
        } else {
            /* The last place where it could be is the retry queue */
            GRilIoRequest* prev = NULL;
            for (req = priv->retry_req; req; req = req->next) {
                if (req->id == id) {
                    GDEBUG("Cancelled request %08x (%08x)", id,
                        req->current_id);
                    if (prev) {
                        prev->next = req->next;
                    } else {
                        priv->retry_req = req->next;
                    }
                    req->next = NULL;
                    req->status = GRILIO_REQUEST_CANCELLED;
                    grilio_channel_drop_request(priv, req);
                    if (notify && req->response) {
                        req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                            req->user_data);
                    }
                    grilio_request_unref(req);
                    grilio_request_unref(block_req);
                    grilio_channel_reset_timeout(self);
                    grilio_channel_schedule_write(self);
                    return TRUE;
                }
                prev = req;
            }
        }

        /* block_req must be in req_table */
        GASSERT(!block_req);
    }
    return FALSE;
}

static
gint
grilio_channel_id_sort_func(
    gconstpointer a,
    gconstpointer b)
{
    const guint id1 = GPOINTER_TO_INT(a);
    const guint id2 = GPOINTER_TO_INT(b);
    return (id1 < id2) ? -1 : (id1 > id2) ? 1 : 0;
}

void
grilio_channel_cancel_all(
    GRilIoChannel* self,
    gboolean notify)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        GRilIoRequest* block_req = NULL;
        GRilIoRequest* req;
        GList* ids;

        if (priv->block_req) {
            /* Don't release the reference or change the status yet.
             * We will update the status and drop the reference after
             * we are done with all other requests. */
            block_req = priv->block_req;
            priv->block_req = NULL;
        }

        if (priv->send_req) {
            /* Current request will be unreferenced after it's sent */
            req = priv->send_req;
            if (req->status != GRILIO_REQUEST_CANCELLED) {
                req->status = GRILIO_REQUEST_CANCELLED;
                grilio_channel_drop_request(priv, req);
                if (notify && req->response) {
                    req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                        req->user_data);
                }
            }
        }
        /* Cancel queued requests */
        while (priv->first_req) {
            req = priv->first_req;
            GDEBUG("Cancelled request %08x (%08x)", req->id, req->current_id);
            grilio_channel_drop_request(priv, req);
            priv->first_req = req->next;
            if (req->next) {
                req->next = NULL;
            } else {
                GASSERT(priv->last_req == req);
                priv->last_req = NULL;
            }
            req->status = GRILIO_REQUEST_CANCELLED;
            if (notify && req->response) {
                req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                    req->user_data);
            }
            grilio_request_unref(req);
        }
        /* Cancel the requests that we have sent which but haven't been
         * replied yet */
        ids = g_list_sort(g_hash_table_get_keys(priv->req_table),
            grilio_channel_id_sort_func);
        if (ids) {
            GList* link = ids;
            while (link) {
                req = g_hash_table_lookup(priv->req_table, link->data);
                if (req) {
                    grilio_request_ref(req);
                    grilio_channel_drop_request(priv, req);
                    req->status = GRILIO_REQUEST_CANCELLED;
                    if (notify && req->response) {
                        req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                            req->user_data);
                    }
                    grilio_request_unref(req);
                }
                link = link->next;
            }
            g_list_free(ids);
        }
        /* And the retry queue */
        while (priv->retry_req) {
            req = priv->retry_req;
            GDEBUG("Cancelled request %08x (%08x)", req->id, req->current_id);
            grilio_channel_drop_request(priv, req);
            priv->retry_req = req->next;
            req->next = NULL;
            req->status = GRILIO_REQUEST_CANCELLED;
            if (notify && req->response) {
                req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                    req->user_data);
            }
            grilio_request_unref(req);
        }
        /* We no longer need the timer */
        if (priv->timeout_id) {
            g_source_remove(priv->timeout_id);
            priv->timeout_id = 0;
            priv->next_deadline = 0;
        }

        /* Unreference the blocking request */
        GASSERT(!block_req || block_req->status == GRILIO_REQUEST_CANCELLED);
        grilio_request_unref(block_req);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
grilio_channel_init(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GRILIO_CHANNEL_TYPE, GRilIoChannelPriv);
    priv->req_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, grilio_request_unref_proc);
    priv->pending = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, grilio_request_unref_proc);
    priv->timeout = GRILIO_TIMEOUT_NONE;
    priv->pending_timeout = GRILIO_DEFAULT_PENDING_TIMEOUT_MS;

    self->priv = priv;
    self->name = "RIL";
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
grilio_channel_dispose(
    GObject* object)
{
    GRilIoChannel* self = GRILIO_CHANNEL(object);
    GRilIoChannelPriv* priv = self->priv;
    grilio_channel_shutdown(self, FALSE);
    grilio_channel_cancel_all(self, TRUE);
    if (priv->send_req) {
        grilio_request_unref(priv->send_req);
        priv->send_req = NULL;
    }
    if (priv->block_ids) {
        g_hash_table_destroy(priv->block_ids);
        priv->block_ids = NULL;
    }
    GASSERT(!priv->owner);
    GASSERT(!priv->owner_queue);
    GASSERT(!priv->block_req);
    G_OBJECT_CLASS(grilio_channel_parent_class)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
grilio_channel_finalize(
    GObject* object)
{
    GRilIoChannel* self = GRILIO_CHANNEL(object);
    GRilIoChannelPriv* priv = self->priv;
    GASSERT(!priv->timeout_id);
    GASSERT(!priv->read_watch_id);
    GASSERT(!priv->write_watch_id);
    GASSERT(!priv->io_channel);
    GASSERT(!priv->block_ids);
    if (priv->pending_timeout_id) {
        g_source_remove(priv->pending_timeout_id);
    }
    g_free(priv->name);
    g_free(priv->read_buf);
    g_hash_table_destroy(priv->req_table);
    g_hash_table_destroy(priv->pending);
    g_slist_free_full(priv->log_list, g_free);
    G_OBJECT_CLASS(grilio_channel_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
grilio_channel_class_init(
    GRilIoChannelClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = grilio_channel_dispose;
    object_class->finalize = grilio_channel_finalize;
    g_type_class_add_private(klass, sizeof(GRilIoChannelPriv));
    grilio_channel_signals[SIGNAL_CONNECTED] =
        g_signal_new(SIGNAL_CONNECTED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    grilio_channel_signals[SIGNAL_UNSOL_EVENT] =
        g_signal_new(SIGNAL_UNSOL_EVENT_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);
    grilio_channel_signals[SIGNAL_ERROR] =
        g_signal_new(SIGNAL_ERROR_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_ERROR);
    grilio_channel_signals[SIGNAL_EOF] =
        g_signal_new(SIGNAL_EOF_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
