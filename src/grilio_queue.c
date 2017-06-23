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
#include "grilio_log.h"

#include <gutil_macros.h>

struct grilio_queue {
    gint refcount;
    GRilIoChannel* channel;
    GRilIoRequest* first_req;
    GRilIoRequest* last_req;
};

GRilIoQueue*
grilio_queue_new(
    GRilIoChannel* channel)
{
    if (G_LIKELY(channel)) {
        GRilIoQueue* queue = g_slice_new0(GRilIoQueue);
        g_atomic_int_set(&queue->refcount, 1);
        queue->channel = grilio_channel_ref(channel);
        return queue;
    }
    return NULL;
}

static
void
grilio_queue_free(
    GRilIoQueue* self)
{
    /* Remove active requests from the queue */
    GRilIoRequest* req = self->first_req;
    while (req) {
        GRilIoRequest* next = req->qnext;
        req->qnext = NULL;
        req->queue = NULL;
        req = next;
    }
    grilio_channel_transaction_finish(self->channel, self);
    grilio_channel_unref(self->channel);
    g_slice_free(GRilIoQueue, self);
}

GRilIoQueue*
grilio_queue_ref(
    GRilIoQueue* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
grilio_queue_unref(
    GRilIoQueue* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            grilio_queue_free(self);
        }
    }
}

static
void
grilio_queue_add(
    GRilIoQueue* self,
    GRilIoRequest* req)
{
    GASSERT(!req->queue);
    req->queue = self;
    if (self->last_req) {
        self->last_req->qnext = req;
        self->last_req = req;
    } else {
        GASSERT(!self->first_req);
        self->first_req = self->last_req = req;
    }
}

void
grilio_queue_remove(
    GRilIoRequest* req)
{
    /* Normally, the first request is getting removed from the queue
     * except for the rare cases when request is being cancelled, which
     * is not something we need to optimize for. */
    GRilIoQueue* queue = req->queue;
    if (queue) {
        GRilIoRequest* ptr;
        GRilIoRequest* prev = NULL;
        for (ptr = queue->first_req; ptr; ptr = ptr->qnext) {
            if (ptr == req) {
                if (prev) {
                    prev->qnext = req->qnext;
                } else {
                    queue->first_req = req->qnext;
                }
                if (req->qnext) {
                    req->qnext = NULL;
                } else {
                    queue->last_req = prev;
                }
                req->queue = NULL;
                break;
            }
            prev = ptr;
        }
    }
}

guint
grilio_queue_send_request(
    GRilIoQueue* self,
    GRilIoRequest* req,
    guint code)
{
    return grilio_queue_send_request_full(self, req, code, NULL, NULL, NULL);
}

guint
grilio_queue_send_request_full(
    GRilIoQueue* self,
    GRilIoRequest* req,
    guint code,
    GRilIoChannelResponseFunc response,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self && (!req || req->status == GRILIO_REQUEST_NEW))) {
        guint id;
        GRilIoRequest* internal_req = NULL;
        if (!req) req = internal_req = grilio_request_new();
        grilio_queue_add(self, req);
        id = grilio_channel_send_request_full(self->channel, req, code,
            response, destroy, user_data);
        /* grilio_channel_send_request_full has no reason to fail */
        GASSERT(id);
        grilio_request_unref(internal_req);
        return id;
    }
    return 0;
}

gboolean
grilio_queue_cancel_request(
    GRilIoQueue* self,
    guint id,
    gboolean notify)
{
    gboolean ok = FALSE;
    if (G_LIKELY(self && id)) {
        GRilIoRequest* req = grilio_channel_get_request(self->channel, id);
        if (req && req->queue == self) {
            ok = grilio_channel_cancel_request(self->channel, id, notify);
            GASSERT(ok);
        }
    }
    return ok;
}

void
grilio_queue_cancel_all(
    GRilIoQueue* self,
    gboolean notify)
{
    if (G_LIKELY(self)) {
        while (self->first_req) {
            GRilIoRequest* req = self->first_req;
            self->first_req = req->qnext;
            if (req->qnext) {
                req->qnext = NULL;
            } else {
                GASSERT(self->last_req == req);
                self->last_req = NULL;
            }
            req->queue = NULL;
            grilio_channel_cancel_request(self->channel, req->id, notify);
        }
    }
}

GRILIO_TRANSACTION_STATE
grilio_queue_transaction_start(
    GRilIoQueue* self)
{
    if (G_LIKELY(self)) {
        return grilio_channel_transaction_start(self->channel, self);
    }
    return GRILIO_TRANSACTION_NONE;
}

GRILIO_TRANSACTION_STATE
grilio_queue_transaction_state(
    GRilIoQueue* self)
{
    if (G_LIKELY(self)) {
        return grilio_channel_transaction_state(self->channel, self);
    }
    return GRILIO_TRANSACTION_NONE;
}

void
grilio_queue_transaction_finish(
    GRilIoQueue* self)
{
    if (G_LIKELY(self)) {
        grilio_channel_transaction_finish(self->channel, self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
