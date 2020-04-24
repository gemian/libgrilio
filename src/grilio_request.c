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

#include "grilio_p.h"
#include "grilio_encode.h"
#include "grilio_log.h"

#include <gutil_macros.h>

static
gboolean
grilio_request_default_retry(
    GRilIoRequest* request,
    int ril_status,
    const void* response_data,
    guint response_len,
    void* user_data)
{
    return ril_status != RIL_E_SUCCESS;
}

GRilIoRequest*
grilio_request_new()
{
    return grilio_request_sized_new(0);
}

GRilIoRequest*
grilio_request_sized_new(
    gsize size)
{
    GRilIoRequest* req = g_slice_new0(GRilIoRequest);
    g_atomic_int_set(&req->refcount, 1);
    req->timeout = GRILIO_TIMEOUT_DEFAULT;
    req->retry = grilio_request_default_retry;
    if (size) {
        req->bytes = g_byte_array_sized_new(size);
    }
    return req;
}

GRilIoRequest*
grilio_request_array_utf8_new(
    guint count,
    const char* value,
    ...)
{
    GRilIoRequest* req = grilio_request_sized_new(4*(count+1));
    grilio_request_append_int32(req, count);
    if (count > 0) {
        guint i;
        va_list args;
        va_start(args, value);
        grilio_request_append_utf8(req, value);
        for (i=1; i<count; i++) {
            grilio_request_append_utf8(req, va_arg(args, const char*));
        }
        va_end (args);
    }
    return req;
}

GRilIoRequest*
grilio_request_array_int32_new(
    guint count,
    gint32 value,
    ...)
{
    GRilIoRequest* req = grilio_request_sized_new(4*(count+1));
    grilio_request_append_int32(req, count);
    if (count > 0) {
        guint i;
        va_list args;
        va_start(args, value);
        grilio_request_append_int32(req, value);
        for (i=1; i<count; i++) {
            grilio_request_append_int32(req, va_arg(args, guint32));
        }
        va_end (args);
    }
    return req;
}

static
void
grilio_request_free(
    GRilIoRequest* req)
{
    GASSERT(!req->next);
    GASSERT(!req->qnext);
    GASSERT(!req->queue);
    if (req->destroy) {
        req->destroy(req->user_data);
    }
    if (req->bytes) {
        g_byte_array_unref(req->bytes);
    }
    g_slice_free(GRilIoRequest, req);
}

GRilIoRequest*
grilio_request_ref(
    GRilIoRequest* req)
{
    if (G_LIKELY(req)) {
        GASSERT(req->refcount > 0);
        g_atomic_int_inc(&req->refcount);
    }
    return req;
}

void
grilio_request_unref(
    GRilIoRequest* req)
{
    if (G_LIKELY(req)) {
        GASSERT(req->refcount > 0);
        if (g_atomic_int_dec_and_test(&req->refcount)) {
            grilio_request_free(req);
        }
    }
}

void
grilio_request_set_blocking(
    GRilIoRequest* req,
    gboolean blocking)
{
    if (G_LIKELY(req)) {
        if (blocking) {
            req->flags |= GRILIO_REQUEST_FLAG_BLOCKING;
        } else {
            req->flags &= ~GRILIO_REQUEST_FLAG_BLOCKING;
        }
    }
}

void
grilio_request_set_timeout(
    GRilIoRequest* req,
    int milliseconds)
{
    if (G_LIKELY(req)) {
        req->timeout = milliseconds;
    }
}

void
grilio_request_set_retry(
    GRilIoRequest* req,
    guint milliseconds,
    int max_retries)
{
    if (G_LIKELY(req)) {
        req->retry_period = milliseconds;
        req->max_retries = max_retries;
    }
}

void
grilio_request_set_retry_func(
    GRilIoRequest* req,
    GRilIoRequestRetryFunc retry)
{
    if (G_LIKELY(req)) {
        req->retry = retry ? retry : grilio_request_default_retry;
    }
}

int
grilio_request_retry_count(
    GRilIoRequest* req)
{
    return G_LIKELY(req) ? req->retry_count : 0;
}

GRILIO_REQUEST_STATUS
grilio_request_status(
    GRilIoRequest* req)
{
    return G_LIKELY(req) ? req->status : GRILIO_REQUEST_INVALID;
}

guint
grilio_request_id(
    GRilIoRequest* req)
{
    return G_LIKELY(req) ? req->id : 0;
}

guint
grilio_request_serial(
    GRilIoRequest* req)
{
    return G_LIKELY(req) ? req->current_id : 0;
}

void
grilio_request_unref_proc(
    gpointer data)
{
    grilio_request_unref(data);
}

void
grilio_request_append_byte(
    GRilIoRequest* req,
    guchar value)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_byte(req->bytes, value);
    }
}

void
grilio_request_append_bytes(
    GRilIoRequest* req,
    const void* data,
    guint len)
{
    if (G_LIKELY(req) && data && len > 0) {
        req->bytes = grilio_encode_bytes(req->bytes, data, len);
    }
}

void
grilio_request_append_int32(
    GRilIoRequest* req,
    guint32 value)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_int32(req->bytes, value);
    }
}

void
grilio_request_append_int32_array(
    GRilIoRequest* req,
    const gint32* values,
    guint count)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_int32_values(req->bytes, values, count);
    }
}

void
grilio_request_append_uint32_array(
    GRilIoRequest* req,
    const guint32* values,
    guint count)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_uint32_values(req->bytes, values, count);
    }
}

void
grilio_request_append_utf8(
    GRilIoRequest* req,
    const char* utf8)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_utf8(req->bytes, utf8);
    }
}

void
grilio_request_append_utf8_chars(
    GRilIoRequest* req,
    const char* utf8,
    gssize num_bytes)
{
    if (G_LIKELY(req)) {
        req->bytes = grilio_encode_utf8_chars(req->bytes, utf8, num_bytes);
    }
}

void
grilio_request_append_format(
    GRilIoRequest* req,
    const char* format,
    ...)
{
    va_list va;
    va_start(va, format);
    grilio_request_append_format_va(req, format, va);
    va_end(va);
}

void
grilio_request_append_format_va(
    GRilIoRequest* req,
    const char* format,
    va_list va)
{
    if (G_LIKELY(req)) {
        char* text = g_strdup_vprintf(format, va);
        grilio_request_append_utf8(req, text);
        g_free(text);
    }
}

const void*
grilio_request_data(
    GRilIoRequest* req)
{
    return (G_LIKELY(req) && req->bytes) ? req->bytes->data : NULL;
}

guint
grilio_request_size(
    GRilIoRequest* req)
{
    return (G_LIKELY(req) && req->bytes) ? req->bytes->len : 0;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
