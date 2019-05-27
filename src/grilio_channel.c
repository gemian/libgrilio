/*
 * Copyright (C) 2015-2019 Jolla Ltd.
 * Copyright (C) 2015-2019 Slava Monich <slava.monich@jolla.com>
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

#include "grilio_p.h"
#include "grilio_parser.h"
#include "grilio_transport_p.h"
#include "grilio_log.h"

#include <gutil_misc.h>
#include <gutil_macros.h>

#define GRILIO_MAX_PACKET_LEN (0x8000)
#define GRILIO_SUB_LEN (4)

typedef struct grilio_channel_event GrilIoChannelEvent;

/* Requests are considered pending for no longer than pending_timeout
 * because pending requests prevent blocking requests from being
 * submitted and we don't want to get stuck forever. */
#define GRILIO_DEFAULT_PENDING_TIMEOUT_MS (30000)

/* Miliseconds to microseconds */
#define MICROSEC(ms) (((gint64)(ms)) * 1000)

/* Log module */
GLOG_MODULE_DEFINE("grilio");

#define LOG_PREFIX(priv) ((priv)->transport->log_prefix)

enum grilio_transport_events {
    TRANSPORT_EVENT_CONNECTED,
    TRANSPORT_EVENT_DISCONNECTED,
    TRANSPORT_EVENT_REQUEST_SENT,
    TRANSPORT_EVENT_RESPONSE,
    TRANSPORT_EVENT_INDICATION,
    TRANSPORT_EVENT_READ_ERROR,
    TRANSPORT_EVENT_WRITE_ERROR,
    TRANSPORT_EVENT_COUNT
};

/* Object definition */
struct grilio_channel_priv {
    GRilIoTransport* transport;
    gulong transport_event_ids[TRANSPORT_EVENT_COUNT];
    GRilIoRequest* send_req;
    guint last_id;
    GHashTable* req_table;
    GHashTable* pending;
    gboolean last_pending;
    int pending_timeout;
    guint pending_timeout_id;
    gint64 next_pending_deadline;
    GSList* log_list;
    GHashTable* gen_ids;

    /* Serialization */
    GHashTable* block_ids;
    GRilIoRequest* block_req;
    GRilIoQueue* owner;
    GSList* owner_queue;

    /* Timeouts */
    int timeout;
    guint timeout_id;
    gint64 next_deadline;

    /* Retry queue (sorted) */
    GRilIoRequest* retry_req;

    /* Send queue */
    GRilIoRequest* first_req;
    GRilIoRequest* last_req;

    /* Injected events */
    gboolean processing_injects;
    guint process_injects_id;
    GrilIoChannelEvent* first_inject;
    GrilIoChannelEvent* last_inject;
};

typedef GObjectClass GRilIoChannelClass;
G_DEFINE_TYPE(GRilIoChannel, grilio_channel, G_TYPE_OBJECT)
#define PARENT_CLASS grilio_channel_parent_class
#define GRILIO_CHANNEL_TYPE (grilio_channel_get_type())
#define GRILIO_CHANNEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        GRILIO_CHANNEL_TYPE, GRilIoChannel))

enum grilio_channel_signal {
    SIGNAL_CONNECTED,
    SIGNAL_UNSOL_EVENT,
    SIGNAL_ERROR,
    SIGNAL_EOF,
    SIGNAL_OWNER,
    SIGNAL_PENDING,
    SIGNAL_COUNT
};

#define SIGNAL_CONNECTED_NAME   "grilio-connected"
#define SIGNAL_UNSOL_EVENT_NAME "grilio-unsol-event"
#define SIGNAL_ERROR_NAME       "grilio-error"
#define SIGNAL_EOF_NAME         "grilio-eof"
#define SIGNAL_OWNER_NAME       "grilio-owner"
#define SIGNAL_PENDING_NAME     "grilio-pending"

#define SIGNAL_UNSOL_EVENT_DETAIL_FORMAT        "%x"
#define SIGNAL_UNSOL_EVENT_DETAIL_MAX_LENGTH    (8)

static guint grilio_channel_signals[SIGNAL_COUNT] = { 0 };

struct grilio_channel_event {
    GrilIoChannelEvent* next;
    guint code;
    void* data;
    guint len;
};

typedef struct grilio_channel_logger {
    int id;
    GrilIoChannelLogFunc log;
    void* user_data;
    gboolean legacy;
} GrilIoChannelLogger;

typedef struct grilio_channel_gen_id_data {
    GRilIoChannelPriv* priv;
    guint id;
    guint timeout_id;
    GRilIoChannelIdCleanupFunc cleanup;
    gpointer user_data;
} GRilIoChannelGenIdData;

static
void
grilio_channel_process_injects(
    GRilIoChannel* self);

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
gboolean
grilio_channel_generic_id_timeout(
    gpointer user_data)
{
    GRilIoChannelGenIdData* data = user_data;
    GRilIoChannelPriv* priv = data->priv;

    GDEBUG("id 0x%08x timed out", data->id);
    data->timeout_id = 0;
    g_hash_table_remove(priv->gen_ids, GINT_TO_POINTER(data->id));
    return G_SOURCE_REMOVE;
}

static
GRilIoChannelGenIdData*
grilio_channel_generic_id_data_new(
    GRilIoChannelPriv* priv,
    guint id,
    guint timeout_ms,
    GRilIoChannelIdCleanupFunc cleanup,
    gpointer user_data)
{
    GRilIoChannelGenIdData* data = g_slice_new(GRilIoChannelGenIdData);

    data->priv = priv;
    data->id = id;
    data->cleanup = cleanup;
    data->user_data = user_data;
    data->timeout_id = g_timeout_add(timeout_ms,
        grilio_channel_generic_id_timeout, data);
    return data;
}

static
void
grilio_channel_generic_id_destroy(
    gpointer value)
{
    if (value) {
        GRilIoChannelGenIdData* data = value;

        if (data->timeout_id) {
            g_source_remove(data->timeout_id);
        }
        if (data->cleanup) {
            /* data->timeout_id gets zeroed on timeout */
            data->cleanup(data->id, !data->timeout_id, data->user_data);
        }
        g_slice_free1(sizeof(*data), data);
    }
}

static
guint
grilio_channel_generate_id(
    GRilIoChannelPriv* priv)
{
    gconstpointer key;

    do {
        (priv->last_id)++;
        key = GINT_TO_POINTER(priv->last_id);
    } while (!priv->last_id || g_hash_table_contains(priv->req_table, key) ||
        (priv->block_ids && g_hash_table_contains(priv->block_ids, key)) ||
        g_hash_table_contains(priv->gen_ids, key));

    return priv->last_id;
}

static
guint
grilio_channel_get_generic_id(
    GRilIoChannelPriv* priv)
{
    const guint id = grilio_channel_generate_id(priv);

    g_hash_table_insert(priv->gen_ids, GINT_TO_POINTER(id), NULL);
    return id;
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
grilio_channel_log(
    GRilIoChannel* self,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    gsize len)
{
    GRilIoChannelPriv* priv = self->priv;
    GSList* link = priv->log_list;
    guint8* legacy_data = NULL;
    gsize legacy_len = 0;
    while (link) {
        GSList* next = link->next;
        GrilIoChannelLogger* logger = link->data;
        if (logger->legacy) {
            if (!legacy_data) {
                guint32* header;
                guint header_len;
                guint ril_code;
                switch (type) {
                case GRILIO_PACKET_REQ:
                    header_len = RIL_REQUEST_HEADER_SIZE;
                    ril_code = code;
                    break;
                default:
                case GRILIO_PACKET_RESP:
                    header_len = RIL_RESPONSE_HEADER_SIZE;
                    ril_code = RIL_PACKET_TYPE_SOLICITED;
                    break;
                case GRILIO_PACKET_RESP_ACK_EXP:
                    header_len = RIL_RESPONSE_HEADER_SIZE;
                    ril_code = RIL_PACKET_TYPE_SOLICITED_ACK_EXP;
                    break;
                case GRILIO_PACKET_UNSOL:
                    header_len = RIL_UNSOL_HEADER_SIZE;
                    ril_code = RIL_PACKET_TYPE_UNSOLICITED;
                    break;
                case GRILIO_PACKET_UNSOL_ACK_EXP:
                    header_len = RIL_UNSOL_HEADER_SIZE;
                    ril_code = RIL_PACKET_TYPE_UNSOLICITED_ACK_EXP;
                    break;
                case GRILIO_PACKET_ACK:
                    header_len = RIL_ACK_HEADER_SIZE;
                    ril_code = RIL_PACKET_TYPE_SOLICITED_ACK;
                    break;
                }

                legacy_len = header_len + len;
                legacy_data = g_malloc(legacy_len);
                memcpy(legacy_data + header_len, data, len);
                header = (guint32*)legacy_data;
                header[0] = GUINT32_TO_RIL(ril_code);

                switch (type) {
                default:
                case GRILIO_PACKET_RESP:
                case GRILIO_PACKET_RESP_ACK_EXP:
                    header[2] = GUINT32_TO_RIL(code); /* status */
                    /* no break */
                case GRILIO_PACKET_REQ:
                case GRILIO_PACKET_ACK:
                    header[1] = GUINT32_TO_RIL(id);
                    break;
                case GRILIO_PACKET_UNSOL_ACK_EXP:
                case GRILIO_PACKET_UNSOL:
                    header[1] = GUINT32_TO_RIL(code);
                    break;
                }
            }
            logger->log(self, type, id, code, legacy_data, legacy_len,
                logger->user_data);
        } else {
            logger->log(self, type, id, code, data, len, logger->user_data);
        }
        link = next;
    }
    g_free(legacy_data);
}

static
guint
grilio_channel_logger_add(
    GRilIoChannel* self,
    gboolean legacy,
    GrilIoChannelLogFunc log,
    void* user_data)
{
    if (G_LIKELY(self && log)) {
        GRilIoChannelPriv* priv = self->priv;
        GrilIoChannelLogger* logger = g_slice_new(GrilIoChannelLogger);

        logger->id = grilio_channel_get_generic_id(priv);
        logger->log = log;
        logger->user_data = user_data;
        logger->legacy = legacy;
        priv->log_list = g_slist_append(priv->log_list, logger);
        return logger->id;
    } else {
        return 0;
    }
}

static
void
grilio_channel_logger_free(
    GrilIoChannelLogger* logger)
{
    g_slice_free(GrilIoChannelLogger, logger);
}

static
void
grilio_channel_logger_free1(
    gpointer logger)
{
    grilio_channel_logger_free(logger);
}

static
void
grilio_channel_update_pending(
    GRilIoChannel* self)
{
    const gboolean has_pending = grilio_channel_has_pending_requests(self);
    GRilIoChannelPriv* priv = self->priv;
    if (priv->last_pending != has_pending) {
        priv->last_pending = has_pending;
        g_signal_emit(self, grilio_channel_signals[SIGNAL_PENDING], 0);
    }
}

static
void
grilio_channel_queue_request(
    GRilIoChannelPriv* priv,
    GRilIoRequest* req)
{
    GASSERT(!req->next);
    GASSERT(req->status == GRILIO_REQUEST_NEW ||
            req->status == GRILIO_REQUEST_RETRY);
    req->status = GRILIO_REQUEST_QUEUED;
    if (priv->last_req) {
        priv->last_req->next = req;
        priv->last_req = req;
    } else {
        GASSERT(!priv->first_req);
        priv->first_req = priv->last_req = req;
    }
    GVERBOSE("Queued %srequest %u (%08x/%08x)", LOG_PREFIX(priv),
        req->code, req->id, req->current_id);
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
    req->current_id = grilio_channel_generate_id(priv);
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
    GRilIoChannel* self,
    gboolean internal_only)
{
    GRilIoChannelPriv* priv = self->priv;
    GRilIoRequest* req = priv->first_req;
    GRilIoRequest* prev = NULL;

    if (req && !(req->flags & GRILIO_REQUEST_FLAG_INTERNAL)) {
        if (internal_only) {
            /* This one is not */
            req = NULL;
        } else {
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
            } else if (grilio_channel_serialized(priv) ||
                       (req->flags & GRILIO_REQUEST_FLAG_BLOCKING)) {
                if (g_hash_table_size(priv->pending)) {
                    /* We need to wait for the currently pending request(s)
                     * to complete or time out before we can submit a blocking
                     * request. */
                    req = NULL;
                }
            }
        }
    }

    if (!req) {
        /* Check if we have any internal requests queued */
        req = priv->first_req;
        while (req && !(req->flags & GRILIO_REQUEST_FLAG_INTERNAL)) {
            prev = req;
            req = req->next;
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
        req->submitted = g_get_monotonic_time();

        GVERBOSE("Sending %srequest %u (%08x/%08x)", LOG_PREFIX(priv),
            req->code, req->id, req->current_id);

        /* Keep track of pending requests, except for those which
         * don't expect any reply */
        if (!(req->flags & GRILIO_REQUEST_FLAG_NO_REPLY)) {
            g_hash_table_insert(priv->pending,
                GINT_TO_POINTER(req->current_id),
                grilio_request_ref(req));
            grilio_channel_reset_pending_timeout(self);
            grilio_channel_update_pending(self);
        }
    }

    return req;
}

static
void
grilio_channel_remove_request(
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
    GRilIoTransport* transport,
    const GError* error,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRilIoChannelPriv* priv = self->priv;

    grilio_transport_shutdown(priv->transport, FALSE);
    g_signal_emit(self, grilio_channel_signals[SIGNAL_ERROR], 0, error);
}

static
void
grilio_channel_handle_connected(
    GRilIoTransport* transport,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);

    self->connected = TRUE;
    self->ril_version = transport->ril_version +
        grilio_transport_version_offset(transport);
    GVERBOSE("Public RIL version %u", self->ril_version);
    g_signal_emit(self, grilio_channel_signals[SIGNAL_CONNECTED], 0);
    grilio_channel_process_injects(self);
    grilio_channel_schedule_write(self);
}

static
void
grilio_channel_handle_disconnected(
    GRilIoTransport* transport,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);

    self->connected = FALSE;
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
    const int req_timeout = (req->timeout > 0) ? req->timeout :
        priv->pending_timeout;
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

    /*
     * To prevent request completion from releasing the last reference
     * to GRilIoChannel, temporarily bump the reference count.
     */
    grilio_channel_ref(self);
    priv->pending_timeout_id = 0;
    g_hash_table_iter_init(&iter, priv->pending);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        GRilIoRequest* req = value;
        const gint64 t = grilio_channel_pending_deadline(priv, req);
        if (t <= now) {
            GDEBUG("Pending %srequest %u (%08x/%08x) expired",
                LOG_PREFIX(priv), req->code, req->id, req->current_id);
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
    grilio_channel_update_pending(self);
    grilio_channel_unref(self);
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
            GDEBUG("%s%srequest %u (%08x/%08x) timed out",
                (priv->block_req == req) ? "Blocking " : "",
                LOG_PREFIX(priv), req->code, req->id, req->current_id);
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
            grilio_channel_update_pending(self);
            pending_expired = TRUE;
        }
        g_hash_table_remove(priv->req_table, GINT_TO_POINTER(req->id));
        if (grilio_request_can_retry(req)) {
            grilio_channel_schedule_retry(priv, req);
        } else {
            grilio_channel_remove_request(priv, req);
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
    grilio_channel_update_pending(self);
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
            priv->next_deadline = deadline;
            priv->timeout_id = (deadline <= now) ?
                g_idle_add(grilio_channel_timeout, self) :
                g_timeout_add(((deadline - now) + 999)/1000,
                    grilio_channel_timeout, self);
        }
    } else if (priv->timeout_id) {
        g_source_remove(priv->timeout_id);
        priv->timeout_id = 0;
        priv->next_deadline = 0;
    }
}

static
void
grilio_channel_request_sent(
    GRilIoChannel* self,
    GRilIoRequest* req)
{
    GRilIoChannelPriv* priv = self->priv;

    /* The request has been sent */
    if (req->status == GRILIO_REQUEST_SENDING) {
        req->status = GRILIO_REQUEST_SENT;
    } else {
        GASSERT(req->status == GRILIO_REQUEST_CANCELLED);
    }

    /* Log it */
    grilio_channel_log(self, GRILIO_PACKET_REQ, req->current_id, req->code,
        grilio_request_data(req), grilio_request_size(req));

    /* If no reply is expected, remove it from req_table */
    if (req->flags & GRILIO_REQUEST_FLAG_NO_REPLY) {
        grilio_channel_remove_request(priv, req);
    }

    /* Submit the next request(s) */
    if (priv->send_req == req) {
        priv->send_req = NULL;
        grilio_request_unref(req);
    }
}

static
gboolean
grilio_channel_send_next_request(
    GRilIoChannel* self)
{
    int req_timeout;
    GRilIoRequest* req;
    GRilIoChannelPriv* priv = self->priv;

    if (!self->connected) {
        GVERBOSE("%s not connected yet", self->name);
        return FALSE;
    }

    if (priv->send_req) {
        /* Request is being sent */
        return FALSE;
    }

    req = priv->send_req;

    if (priv->block_req && !req) {
        /* Try to dequeue an internal request */
        req = priv->send_req = grilio_channel_dequeue_request(self, TRUE);
        if (!req) {
            GVERBOSE("%s waiting for request %08x to complete", self->name,
                priv->block_req->current_id);
            return FALSE;
        }
    }

    if (!req) {
        /* Nothing stops us from dequeueing any request */
        req = priv->send_req = grilio_channel_dequeue_request(self, FALSE);
        if (!req) {
            /* There is nothing to send, remove the watch */
            GVERBOSE("%s has nothing to send", self->name);
            return FALSE;
        }
    }

    req_timeout = req->timeout;
    if (req_timeout == GRILIO_TIMEOUT_DEFAULT && priv->timeout > 0) {
        req_timeout = priv->timeout;
    }

    if (!(req->flags & GRILIO_REQUEST_FLAG_INTERNAL) &&
        ((req->flags & GRILIO_REQUEST_FLAG_BLOCKING) ||
         grilio_channel_serialized(priv))) {
        /* Block next requests from being sent while this one is pending */
        priv->block_req = grilio_request_ref(req);
    }

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

    if (grilio_transport_send(priv->transport, req, req->code) ==
        GRILIO_SEND_OK) {
        grilio_channel_request_sent(self, req);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
grilio_channel_handle_request_sent(
    GRilIoTransport* transport,
    GRilIoRequest* req,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);

    grilio_channel_request_sent(self, req);
    grilio_channel_schedule_write(self);
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
    if (self->connected) {
        while (grilio_channel_send_next_request(self));
#if GUTIL_LOG_VERBOSE
        if (!self->priv->send_req && !self->priv->first_req) {
            GVERBOSE("%squeue empty", LOG_PREFIX(self->priv));
        }
#endif
    }
}

static
void
grilio_channel_queue_ack(
     GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;
    GRilIoRequest* req = grilio_request_new();
    req->id = req->current_id = grilio_channel_generate_id(priv);
    req->code = RIL_RESPONSE_ACKNOWLEDGEMENT;
    /* These packets are not subject to serialization and expect no reply */
    req->flags |= GRILIO_REQUEST_FLAG_INTERNAL | GRILIO_REQUEST_FLAG_NO_REPLY;
    g_hash_table_insert(priv->req_table, GINT_TO_POINTER(req->id),
        grilio_request_ref(req));
    grilio_channel_queue_request(priv, req);
}

static
void
grilio_channel_handle_response(
    GRilIoTransport* transport,
    GRILIO_RESPONSE_TYPE type,
    guint id,
    int status,
    const void* resp,
    guint len,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRilIoChannelPriv* priv = self->priv;
    const void* key = GINT_TO_POINTER(id);
    GRilIoRequest* req = g_hash_table_lookup(priv->req_table, key);
    GRILIO_PACKET_TYPE ptype;

    switch (type) {
    case GRILIO_RESPONSE_SOLICITED_ACK:
        GDEBUG("%08x acked", id);
        grilio_channel_log(self, GRILIO_PACKET_ACK, id, 0, resp, len);
        /* The request is not done yet */
        return;
    case GRILIO_RESPONSE_SOLICITED_ACK_EXP:
        grilio_channel_queue_ack(self);
        ptype = GRILIO_PACKET_RESP_ACK_EXP;
        break;
    default: GASSERT(type == GRILIO_RESPONSE_SOLICITED); /* no break */
    case GRILIO_RESPONSE_SOLICITED:
        ptype = GRILIO_PACKET_RESP;
        break;
    }
        
    /* Remove this id from the list of pending requests */
    if (g_hash_table_remove(priv->pending, key)) {
        /* Reset submit time */
        if (req) req->submitted = 0;
        grilio_channel_reset_pending_timeout(self);
    }

    /* Logger receives everything except the length */
    grilio_channel_log(self, ptype, id, status, resp, len);

    if (priv->block_req && priv->block_req->current_id == id) {
        /* Blocking request has completed */
        grilio_request_unref(priv->block_req);
        priv->block_req = NULL;
    }

    /* Handle the case if we receive a response with the id of the
     * packet which we haven't sent yet. */
    if (req && req->status == GRILIO_REQUEST_SENT) {
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
            grilio_channel_remove_request(priv, req);
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
    grilio_channel_update_pending(self);
}

static
void
grilio_channel_emit_unsol_event(
    GRilIoChannel* self,
    guint code,
    const void* data,
    guint len)
{
    GQuark detail;
    char signame[SIGNAL_UNSOL_EVENT_DETAIL_MAX_LENGTH + 1];

    /* We should be connected when we are doing this (unless we have just
     * received RIL_UNSOL_RIL_CONNECTED event) */
    GASSERT(self->connected || code == RIL_UNSOL_RIL_CONNECTED);

    /* Event code is the detail */
    snprintf(signame, sizeof(signame), SIGNAL_UNSOL_EVENT_DETAIL_FORMAT, code);
    detail = g_quark_from_string(signame);
    g_signal_emit(self, grilio_channel_signals[SIGNAL_UNSOL_EVENT],
        detail, code, data, len);
}

static
void
grilio_channel_process_injects(
    GRilIoChannel* self)
{
    GRilIoChannelPriv* priv = self->priv;

    GASSERT(!priv->processing_injects);
    priv->processing_injects = TRUE;

    while (priv->first_inject) {
        GrilIoChannelEvent* e = priv->first_inject;
        priv->first_inject = e->next;
        if (!priv->first_inject) {
            priv->last_inject = NULL;
        }
        GDEBUG("Injecting event %u, %u byte(s)", e->code, e->len);
        grilio_channel_emit_unsol_event(self, e->code, e->data, e->len);
        g_free(e->data);
        g_slice_free(GrilIoChannelEvent, e);
    }

    priv->processing_injects = FALSE;
}

static
gboolean
grilio_channel_process_injects_cb(
    gpointer user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRilIoChannelPriv* priv = self->priv;

    GASSERT(priv->process_injects_id);
    priv->process_injects_id = 0;
    grilio_channel_process_injects(self);
    return G_SOURCE_REMOVE;
}

static
void
grilio_drop_pending_injects(
    GRilIoChannelPriv* priv)
{
    if (priv->process_injects_id) {
        g_source_remove(priv->process_injects_id);
        priv->process_injects_id = 0;
    }
    if (priv->first_inject) {
        GrilIoChannelEvent* e;
        for (e = priv->first_inject; e; e = e->next) g_free(e->data);
        g_slice_free_chain(GrilIoChannelEvent, priv->first_inject, next);
        priv->first_inject = priv->last_inject = NULL;
    }
}

static
void
grilio_channel_handle_indication(
    GRilIoTransport* transport,
    GRILIO_INDICATION_TYPE type,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    GRilIoChannel* self = GRILIO_CHANNEL(user_data);
    GRILIO_PACKET_TYPE ptype;

    switch (type) {
    case GRILIO_INDICATION_UNSOLICITED_ACK_EXP:
        grilio_channel_queue_ack(self);
        ptype = GRILIO_PACKET_UNSOL_ACK_EXP;
        break;
    default: GASSERT(type == GRILIO_INDICATION_UNSOLICITED); /* no break */
    case GRILIO_INDICATION_UNSOLICITED:
        ptype = GRILIO_PACKET_UNSOL;
        break;
    }

    /* Loggers get the whole thing except the length */
    grilio_channel_log(self, ptype, 0, code, data, len);

    /* Event handler gets event code and the data separately */
    grilio_channel_emit_unsol_event(self, code, data, len);
    grilio_channel_schedule_write(self);
}

/*==========================================================================*
 * API
 *==========================================================================*/

GRilIoChannel*
grilio_channel_new_socket(
    const char* path,
    const char* sub)
{
    GRilIoTransport* transport = grilio_transport_socket_new_path(path, sub);
    GRilIoChannel* channel = grilio_channel_new(transport);

    grilio_transport_unref(transport);
    return channel;
}

GRilIoChannel*
grilio_channel_new_fd(
    int fd,
    const char* sub,
    gboolean own_fd)
{
    GRilIoTransport* transport = grilio_transport_socket_new(fd, sub, own_fd);
    GRilIoChannel* channel = grilio_channel_new(transport);

    grilio_transport_unref(transport);
    return channel;
}

GRilIoChannel*
grilio_channel_new(
    GRilIoTransport* transport)
{
    if (G_LIKELY(transport)) {
        GRilIoChannel* self = g_object_new(GRILIO_CHANNEL_TYPE, NULL);
        GRilIoChannelPriv* priv = self->priv;

        priv->transport = grilio_transport_ref(transport);
        priv->transport_event_ids[TRANSPORT_EVENT_CONNECTED] =
            grilio_transport_add_connected_handler(transport,
                grilio_channel_handle_connected, self);
        priv->transport_event_ids[TRANSPORT_EVENT_DISCONNECTED] =
            grilio_transport_add_disconnected_handler(transport,
                grilio_channel_handle_disconnected, self);
        priv->transport_event_ids[TRANSPORT_EVENT_REQUEST_SENT] =
            grilio_transport_add_request_sent_handler(transport,
                grilio_channel_handle_request_sent, self);
        priv->transport_event_ids[TRANSPORT_EVENT_RESPONSE] =
            grilio_transport_add_response_handler(transport,
                grilio_channel_handle_response, self);
        priv->transport_event_ids[TRANSPORT_EVENT_INDICATION] =
            grilio_transport_add_indication_handler(transport,
                grilio_channel_handle_indication, self);
        priv->transport_event_ids[TRANSPORT_EVENT_READ_ERROR] =
            grilio_transport_add_read_error_handler(transport,
                grilio_channel_handle_error, self);
        priv->transport_event_ids[TRANSPORT_EVENT_WRITE_ERROR] =
            grilio_transport_add_write_error_handler(transport,
                grilio_channel_handle_error, self);
        grilio_transport_set_channel(priv->transport, self);
        return self;
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

        grilio_transport_shutdown(priv->transport, FALSE);
        grilio_drop_pending_injects(priv);
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

        grilio_transport_set_name(priv->transport, name);
        self->name = priv->transport->name;
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
        id = grilio_channel_generate_id(priv);
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
                if (priv->block_req &&
                    !(priv->block_req->flags & GRILIO_REQUEST_FLAG_BLOCKING)) {
                    grilio_request_unref(priv->block_req);
                    priv->block_req = NULL;
                }
                grilio_channel_schedule_write(self);
            }
        }
    }
}

gboolean
grilio_channel_has_pending_requests(
    GRilIoChannel* self)
{
    return G_LIKELY(self) && g_hash_table_size(self->priv->pending) > 0;
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

gulong
grilio_channel_add_owner_changed_handler(
    GRilIoChannel* self,
    GRilIoChannelEventFunc func,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_OWNER_NAME, G_CALLBACK(func), arg) : 0;
}

gulong
grilio_channel_add_pending_changed_handler(
    GRilIoChannel* self,
    GRilIoChannelEventFunc func,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_PENDING_NAME, G_CALLBACK(func), arg) : 0;
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
    return grilio_channel_logger_add(self, TRUE, log, user_data);
}

guint
grilio_channel_add_logger2(
    GRilIoChannel* self,
    GrilIoChannelLogFunc log,
    void* user_data)
{
    return grilio_channel_logger_add(self, FALSE, log, user_data);
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
                grilio_channel_logger_free(logger);
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
        const guint id = grilio_channel_generate_id(priv);
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
            g_signal_emit(self, grilio_channel_signals[SIGNAL_OWNER], 0);
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
            g_signal_emit(self, grilio_channel_signals[SIGNAL_OWNER], 0);
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
                grilio_channel_remove_request(priv, req);
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
                    GDEBUG("Cancelled %srequest %u (%08x/%08x)",
                        LOG_PREFIX(priv), req->code, req->id, req->current_id);
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
                    grilio_channel_remove_request(priv, req);
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
            grilio_channel_remove_request(priv, req);
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
                    GDEBUG("Cancelled %srequest %u (%08x/%08x)",
                        LOG_PREFIX(priv), req->code, req->id, req->current_id);
                    if (prev) {
                        prev->next = req->next;
                    } else {
                        priv->retry_req = req->next;
                    }
                    req->next = NULL;
                    req->status = GRILIO_REQUEST_CANCELLED;
                    grilio_channel_remove_request(priv, req);
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
                grilio_channel_remove_request(priv, req);
                if (notify && req->response) {
                    req->response(self, GRILIO_STATUS_CANCELLED, NULL, 0,
                        req->user_data);
                }
            }
        }
        /* Cancel queued requests */
        while (priv->first_req) {
            req = priv->first_req;
            GDEBUG("Cancelled %srequest %u (%08x/%08x)", LOG_PREFIX(priv),
                req->code, req->id, req->current_id);
            grilio_channel_remove_request(priv, req);
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
                    grilio_channel_remove_request(priv, req);
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
            GDEBUG("Cancelled %srequest %u (%08x/%08x)", LOG_PREFIX(priv),
                req->code, req->id, req->current_id);
            grilio_channel_remove_request(priv, req);
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

/**
 * Same as grilio_channel_cancel_request but also removes the cancelled
 * request from the pending list so that it no longer prevents blocking
 * requests from being submitted. This should only be used for those
 * requests which RIL doesn't bother to reply to (which is not supposed
 * to happen but unfortunately does).
 */
void
grilio_channel_drop_request(
    GRilIoChannel* self,
    guint id)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        gpointer key = GINT_TO_POINTER(id);
        GRilIoRequest* req;
        grilio_channel_cancel_request(self, id, FALSE);
        req = g_hash_table_lookup(priv->pending, key);
        if (req) {
            GDEBUG("Dropped pending %srequest %u (%08x/%08x)",
                LOG_PREFIX(priv), req->code, req->id, req->current_id);
            req->submitted = 0;
            g_hash_table_remove(priv->pending, key);
            grilio_channel_reset_pending_timeout(self);
            grilio_channel_schedule_write(self);
            grilio_channel_update_pending(self);
        }
    }
}

/**
 * Queues an unsolicited event as if it arrived from rild. No socket
 * communication is involed, it's a purely local action. Loggers don't
 * see it either (should they?).
 *
 * Since 1.0.21
 */
void
grilio_channel_inject_unsol_event(
    GRilIoChannel* self,
    guint code,
    const void* data,
    guint len)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        GrilIoChannelEvent* e = g_slice_new0(GrilIoChannelEvent);

        e->code = code;
        e->data = g_memdup(data, len);
        e->len = len;

        /* Queue the event */
        if (priv->last_inject) {
            priv->last_inject->next = e;
            priv->last_inject = e;
        } else {
            GASSERT(!priv->first_inject);
            priv->first_inject = priv->last_inject = e;
        }

        /* And schedule the callback if necessary */
        if (self->connected &&
            !priv->process_injects_id &&
            !priv->processing_injects) {
            priv->process_injects_id =
                g_idle_add(grilio_channel_process_injects_cb, self);
        }
    }
}

guint
grilio_channel_get_id(
    GRilIoChannel* self)
{
    return G_LIKELY(self) ? grilio_channel_get_generic_id(self->priv) : 0;
}

gboolean
grilio_channel_release_id(
    GRilIoChannel* self,
    guint id)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;

        return g_hash_table_remove(priv->gen_ids, GINT_TO_POINTER(id));
    }
    return FALSE;
}

guint
grilio_channel_get_id_with_timeout(
    GRilIoChannel* self,
    guint timeout_ms,
    GRilIoChannelIdCleanupFunc cleanup,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        GRilIoChannelPriv* priv = self->priv;
        const guint id = grilio_channel_generate_id(priv);

        g_hash_table_insert(priv->gen_ids, GINT_TO_POINTER(id),
            grilio_channel_generic_id_data_new(priv, id, timeout_ms ?
                timeout_ms : GRILIO_DEFAULT_PENDING_TIMEOUT_MS,
                cleanup, user_data));
        return id;
    }
    return 0;
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
    priv->gen_ids = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, grilio_channel_generic_id_destroy);

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
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
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

    GASSERT(!priv->first_inject);
    GASSERT(!priv->last_inject);
    GASSERT(!priv->process_injects_id);
    GASSERT(!priv->timeout_id);
    GASSERT(!priv->block_ids);
    grilio_transport_remove_all_handlers(priv->transport,
        priv->transport_event_ids);
    grilio_transport_set_channel(priv->transport, NULL);
    grilio_transport_unref(priv->transport);
    if (priv->pending_timeout_id) {
        g_source_remove(priv->pending_timeout_id);
    }
    g_hash_table_destroy(priv->gen_ids);
    g_hash_table_destroy(priv->req_table);
    g_hash_table_destroy(priv->pending);
    g_slist_free_full(priv->log_list, grilio_channel_logger_free1);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
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
    grilio_channel_signals[SIGNAL_OWNER] =
        g_signal_new(SIGNAL_OWNER_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    grilio_channel_signals[SIGNAL_PENDING] =
        g_signal_new(SIGNAL_PENDING_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
