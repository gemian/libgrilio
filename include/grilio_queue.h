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

#ifndef GRILIO_QUEUE_H
#define GRILIO_QUEUE_H

#include "grilio_channel.h"

G_BEGIN_DECLS

GRilIoQueue*
grilio_queue_new(
    GRilIoChannel* channel);

GRilIoQueue*
grilio_queue_ref(
    GRilIoQueue* queue);

void
grilio_queue_unref(
    GRilIoQueue* queue);

guint
grilio_queue_send_request(
    GRilIoQueue* queue,
    GRilIoRequest* req,
    guint code);

guint
grilio_queue_send_request_full(
    GRilIoQueue* queue,
    GRilIoRequest* req,
    guint code,
    GRilIoChannelResponseFunc response,
    GDestroyNotify destroy,
    void* user_data);

gboolean
grilio_queue_cancel_request(
    GRilIoQueue* queue,
    guint id,
    gboolean notify);

void
grilio_queue_cancel_all(
    GRilIoQueue* queue,
    gboolean notify);

GRILIO_TRANSACTION_STATE
grilio_queue_transaction_start(
    GRilIoQueue* queue);

GRILIO_TRANSACTION_STATE
grilio_queue_transaction_state(
    GRilIoQueue* queue);

void
grilio_queue_transaction_finish(
    GRilIoQueue* queue);

G_END_DECLS

#endif /* GRILIO_QUEUE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
