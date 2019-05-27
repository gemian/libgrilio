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

#ifndef GRILIO_PRIVATE_H
#define GRILIO_PRIVATE_H

#include "grilio_request.h"
#include "grilio_channel.h"
#include "grilio_queue.h"

/* Byte order of the RIL payload (native?) */
#define GUINT32_FROM_RIL(x) (x) /* GUINT32_FROM_LE(x) ? */
#define GUINT32_TO_RIL(x)   (x) /* GUINT32_TO_LE(x) ? */

/* RIL constants (for legacy loggers and socket transport) */
#define RIL_REQUEST_HEADER_SIZE (8)    /* code, id */
#define RIL_RESPONSE_HEADER_SIZE (12)  /* type, id, status */
#define RIL_ACK_HEADER_SIZE (8)        /* type, id */
#define RIL_UNSOL_HEADER_SIZE (8)      /* type, code */
#define RIL_MAX_HEADER_SIZE (12)

#define RIL_RESPONSE_ACKNOWLEDGEMENT (800)
#define RIL_UNSOL_RIL_CONNECTED (1034)
#define RIL_E_SUCCESS (0)

/* Packet types (first word of the payload) */
typedef enum ril_packet_type {
    RIL_PACKET_TYPE_SOLICITED = 0,
    RIL_PACKET_TYPE_UNSOLICITED = 1,
    RIL_PACKET_TYPE_SOLICITED_ACK = 2,
    RIL_PACKET_TYPE_SOLICITED_ACK_EXP = 3,
    RIL_PACKET_TYPE_UNSOLICITED_ACK_EXP = 4
} RIL_PACKET_TYPE;

/*
 * 12 bytes are reserved for the packet header:
 *
 * [0] Length
 * [1] Request code
 * [2] Request id 
 */
struct grilio_request {
    gint refcount;
    int timeout;
    guint32 code;
    guint id;
    guint current_id;
    gint64 deadline;
    gint64 submitted;
    GRILIO_REQUEST_STATUS status;
    int max_retries;
    int retry_count;
    guint retry_period;
    GByteArray* bytes;
    GRilIoRequest* next;
    GRilIoRequest* qnext;
    GRilIoQueue* queue;
    GRilIoRequestRetryFunc retry;
    GRilIoChannelResponseFunc response;
    GDestroyNotify destroy;
    void* user_data;
    gboolean flags;

#define GRILIO_REQUEST_FLAG_BLOCKING    (0x01)
#define GRILIO_REQUEST_FLAG_INTERNAL    (0x02)
#define GRILIO_REQUEST_FLAG_NO_REPLY    (0x04)
};

typedef
void
(*GRilIoChannelIdCleanupFunc)(
    guint id,
    gboolean timeout,
    gpointer user_data);

G_GNUC_INTERNAL
void
grilio_request_unref_proc(
    gpointer data);

G_GNUC_INTERNAL
void
grilio_queue_remove(
    GRilIoRequest* req);

G_GNUC_INTERNAL
GRilIoRequest*
grilio_channel_get_request(
    GRilIoChannel* channel,
    guint id);

G_GNUC_INTERNAL
void
grilio_channel_set_pending_timeout(
    GRilIoChannel* channel,
    int ms);

G_GNUC_INTERNAL
GRILIO_TRANSACTION_STATE
grilio_channel_transaction_start(
    GRilIoChannel* channel,
    GRilIoQueue* queue);

G_GNUC_INTERNAL
GRILIO_TRANSACTION_STATE
grilio_channel_transaction_state(
    GRilIoChannel* channel,
    GRilIoQueue* queue);

G_GNUC_INTERNAL
void
grilio_channel_transaction_finish(
    GRilIoChannel* channel,
    GRilIoQueue* queue);

G_GNUC_INTERNAL
guint
grilio_channel_get_id(
    GRilIoChannel* channel);

G_GNUC_INTERNAL
gboolean
grilio_channel_release_id(
    GRilIoChannel* channel,
    guint id);

G_GNUC_INTERNAL
guint
grilio_channel_get_id_with_timeout(
    GRilIoChannel* channel,
    guint timeout_ms,
    GRilIoChannelIdCleanupFunc cleanup,
    gpointer user_data);

G_INLINE_FUNC gboolean
grilio_request_can_retry(GRilIoRequest* req)
    { return req->max_retries < 0 || req->max_retries > req->retry_count; }

#endif /* GRILIO_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
