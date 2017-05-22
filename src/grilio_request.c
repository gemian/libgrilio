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
    req->bytes = g_byte_array_sized_new(GRILIO_REQUEST_HEADER_SIZE + size);
    g_byte_array_set_size(req->bytes, GRILIO_REQUEST_HEADER_SIZE);
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
    g_byte_array_unref(req->bytes);
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
        g_byte_array_set_size(req->bytes, req->bytes->len + 1);
        req->bytes->data[req->bytes->len-1] = value;
    }
}

void
grilio_request_append_bytes(
    GRilIoRequest* req,
    const void* data,
    guint len)
{
    if (G_LIKELY(req) && data && len > 0) {
        const gsize old_size = req->bytes->len;
        g_byte_array_set_size(req->bytes, old_size + len);
        memcpy(req->bytes->data + old_size, data, len);
    }
}

void
grilio_request_append_int32(
    GRilIoRequest* req,
    guint32 value)
{
    if (G_LIKELY(req)) {
        guint32* ptr;
        g_byte_array_set_size(req->bytes, req->bytes->len + 4);
        ptr = (guint32*)(req->bytes->data + (req->bytes->len-4));
        *ptr = GUINT32_TO_RIL(value);
    }
}

void
grilio_request_append_int32_array(
    GRilIoRequest* req,
    const gint32* values,
    guint n)
{
    return grilio_request_append_uint32_array(req, (const guint32*)values, n);
}

void
grilio_request_append_uint32_array(
    GRilIoRequest* req,
    const guint32* values,
    guint count)
{
    if (G_LIKELY(req)) {
        guint i;
        for (i=0; i<count; i++) {
            grilio_request_append_int32(req, values[i]);
        }
    }
}

void
grilio_request_append_utf8(
    GRilIoRequest* req,
    const char* utf8)
{
    grilio_request_append_utf8_chars(req, utf8, utf8 ? strlen(utf8) : 0);
}

void
grilio_request_append_utf8_chars(
    GRilIoRequest* req,
    const char* utf8,
    gssize num_bytes)
{
    if (G_LIKELY(req)) {
        const gsize old_size = req->bytes->len;
        if (utf8) {
            const char* end = utf8;
            g_utf8_validate(utf8, num_bytes, &end);
            num_bytes = end - utf8;
        } else {
            num_bytes = 0;
        }
        if (num_bytes > 0) {
            glong len = g_utf8_strlen(utf8, num_bytes);
            gsize padded_len = G_ALIGN4((len+1)*2);
            guint32* len_ptr;
            gunichar2* utf16_ptr;

            /* Preallocate space */
            g_byte_array_set_size(req->bytes, old_size + padded_len + 4);
            len_ptr = (guint32*)(req->bytes->data + old_size);
            utf16_ptr = (gunichar2*)(len_ptr + 1);

            /* TODO: this could be optimized for ASCII strings, i.e. if
             * len equals num_bytes */
            if (len > 0) {
                glong utf16_len = 0;
                gunichar2* utf16 = g_utf8_to_utf16(utf8, num_bytes, NULL,
                    &utf16_len, NULL);
                if (utf16) {
                    len = utf16_len;
                    padded_len = G_ALIGN4((len+1)*2);
                    memcpy(utf16_ptr, utf16, (len+1)*2);
                    g_free(utf16);
                }
            }

            /* Actual length */
            *len_ptr = GUINT32_TO_RIL(len);

            /* Zero padding */
            if (padded_len - (len + 1)*2) {
                memset(utf16_ptr + (len + 1), 0, padded_len - (len + 1)*2);
            }

            /* Correct the packet size if necessaary */
            g_byte_array_set_size(req->bytes, old_size + padded_len + 4);
        } else if (utf8) {
            /* Empty string */
            guint16* ptr16;
            g_byte_array_set_size(req->bytes, old_size + 8);
            ptr16 = (guint16*)(req->bytes->data + old_size);
            ptr16[0] = ptr16[1] = ptr16[2] = 0; ptr16[3] = 0xffff;
        } else {
            /* NULL string */
            grilio_request_append_int32(req, -1);
        }
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
    char* text = g_strdup_vprintf(format, va);
    grilio_request_append_utf8(req, text);
    g_free(text);
}

const void*
grilio_request_data(
    GRilIoRequest* req)
{
    if (G_LIKELY(req)) {
        GASSERT(req->bytes->len >= GRILIO_REQUEST_HEADER_SIZE);
        return req->bytes->data + GRILIO_REQUEST_HEADER_SIZE;
    } else {
        return NULL;
    }
}

guint
grilio_request_size(
    GRilIoRequest* req)
{
    if (G_LIKELY(req)) {
        GASSERT(req->bytes->len >= GRILIO_REQUEST_HEADER_SIZE);
        return req->bytes->len - GRILIO_REQUEST_HEADER_SIZE;
    } else {
        return 0;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
