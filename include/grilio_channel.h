/*
 * Copyright (C) 2015-2018 Jolla Ltd.
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

#ifndef GRILIO_CHANNEL_H
#define GRILIO_CHANNEL_H

#include "grilio_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct grilio_channel_priv GRilIoChannelPriv;

struct grilio_channel {
    GObject object;
    GRilIoChannelPriv* priv;
    const char* name;
    gboolean connected;
    guint ril_version;
};

/* Status values for GRilIoResponseFunc. Zero means success,
 * negative values - GrilIo errors, positive - RIL errors */
#define GRILIO_STATUS_TIMEOUT   (-2)
#define GRILIO_STATUS_CANCELLED (-1)
#define GRILIO_STATUS_OK        (0)

typedef
void
(*GRilIoChannelEventFunc)(
    GRilIoChannel* channel,
    void* user_data);

typedef
void
(*GRilIoChannelErrorFunc)(
    GRilIoChannel* channel,
    const GError* error,
    void* user_data);

typedef
void
(*GRilIoChannelUnsolEventFunc)(
    GRilIoChannel* channel,
    guint code,
    const void* data,
    guint len,
    void* user_data);

typedef
void
(*GRilIoChannelResponseFunc)(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data);

typedef
void
(*GrilIoChannelLogFunc)(
    GRilIoChannel* channel,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    guint data_len,
    void* user_data);

GRilIoChannel*
grilio_channel_new(
    GRilIoTransport* transport);

GRilIoChannel*
grilio_channel_new_socket(
    const char* path,
    const char* subscription);

GRilIoChannel*
grilio_channel_new_fd(
    int fd,
    const char* subscription,
    gboolean can_close);

void
grilio_channel_shutdown(
    GRilIoChannel* channel,
    gboolean flush);

GRilIoChannel*
grilio_channel_ref(
    GRilIoChannel* channel);

void
grilio_channel_unref(
    GRilIoChannel* channel);

void
grilio_channel_set_timeout(
    GRilIoChannel* channel,
    int milliseconds);

void
grilio_channel_set_name(
    GRilIoChannel* channel,
    const char* name);

guint
grilio_channel_serialize(
    GRilIoChannel* self);

void
grilio_channel_deserialize(
    GRilIoChannel* self,
    guint id);

gboolean
grilio_channel_has_pending_requests(
    GRilIoChannel* self);

guint
grilio_channel_add_logger(
    GRilIoChannel* channel,
    GrilIoChannelLogFunc log,
    void* user_data);

/*
 * Logger callbacks registered with grilio_channel_add_logger2 only receive
 * packet payload, without the header. It's more efficient.
 *
 * Since 1.0.25
 */
guint
grilio_channel_add_logger2(
    GRilIoChannel* channel,
    GrilIoChannelLogFunc log,
    void* user_data);

guint
grilio_channel_add_default_logger(
    GRilIoChannel* channel,
    int level);

void
grilio_channel_remove_logger(
    GRilIoChannel* channel,
    guint id);

gulong
grilio_channel_add_connected_handler(
    GRilIoChannel* channel,
    GRilIoChannelEventFunc func,
    void* arg);

gulong
grilio_channel_add_disconnected_handler(
    GRilIoChannel* channel,
    GRilIoChannelEventFunc func,
    void* arg);

gulong
grilio_channel_add_unsol_event_handler(
    GRilIoChannel* channel,
    GRilIoChannelUnsolEventFunc func,
    guint code,
    void* arg);

gulong
grilio_channel_add_error_handler(
    GRilIoChannel* channel,
    GRilIoChannelErrorFunc func,
    void* arg);

gulong
grilio_channel_add_owner_changed_handler(
    GRilIoChannel* channel,
    GRilIoChannelEventFunc func,
    void* arg);

gulong
grilio_channel_add_pending_changed_handler(
    GRilIoChannel* channel,
    GRilIoChannelEventFunc func,
    void* arg);

void
grilio_channel_remove_handler(
    GRilIoChannel* channel,
    gulong id);

void
grilio_channel_remove_handlers(
    GRilIoChannel* channel,
    gulong *ids,
    guint count);

#define grilio_channel_remove_all_handlers(channel,ids) \
    grilio_channel_remove_handlers(channel, ids, G_N_ELEMENTS(ids))

guint
grilio_channel_send_request(
    GRilIoChannel* channel,
    GRilIoRequest* req,
    guint code);

guint
grilio_channel_send_request_full(
    GRilIoChannel* channel,
    GRilIoRequest* req,
    guint code,
    GRilIoChannelResponseFunc response,
    GDestroyNotify destroy,
    void* user_data);

gboolean
grilio_channel_retry_request(
    GRilIoChannel* channel,
    guint id);

gboolean
grilio_channel_cancel_request(
    GRilIoChannel* channel,
    guint id,
    gboolean notify);

void
grilio_channel_cancel_all(
    GRilIoChannel* channel,
    gboolean notify);

void
grilio_channel_drop_request(
    GRilIoChannel* channel,
    guint id);

void
grilio_channel_inject_unsol_event(
    GRilIoChannel* self,
    guint code,
    const void* data,
    guint len);

G_END_DECLS

#endif /* GRILIO_CHANNEL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
