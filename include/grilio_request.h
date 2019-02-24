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

#ifndef GRILIO_REQUEST_H
#define GRILIO_REQUEST_H

#include "grilio_types.h"

G_BEGIN_DECLS

typedef enum grilio_request_status {
    GRILIO_REQUEST_INVALID = -1,
    GRILIO_REQUEST_NEW,
    GRILIO_REQUEST_QUEUED,
    GRILIO_REQUEST_SENDING,
    GRILIO_REQUEST_SENT,
    GRILIO_REQUEST_CANCELLED,
    GRILIO_REQUEST_DONE,
    GRILIO_REQUEST_RETRY
} GRILIO_REQUEST_STATUS;

/*
 * GRilIoRequestRetryFunc

 * If request retry is enabled with grilio_request_set_retry(), then this
 * callback is invoiked to check whether the request should be retried,
 * based on the status received from RIL. If it returns TRUE, the request
 * is retried at some point in the future, otherwise it gets completed
 * right away. The default callback returns (ril_status != RIL_E_SUCCESS).
 *
 * user_data is the pointer passed to grilio_xxx_send_request_full()
 * when the request was submitted or NULL if it was submitted with with
 * grilio_xxx_send_request().
 */
typedef
gboolean
(*GRilIoRequestRetryFunc)(
    GRilIoRequest* request,
    int ril_status,
    const void* response_data,
    guint response_len,
    void* user_data);

GRilIoRequest*
grilio_request_new(void);

GRilIoRequest*
grilio_request_sized_new(
    gsize size);

GRilIoRequest*
grilio_request_array_utf8_new(
    guint count,
    const char* value,
    ...);

GRilIoRequest*
grilio_request_array_int32_new(
    guint count,
    gint32 value,
    ...);

GRilIoRequest*
grilio_request_ref(
    GRilIoRequest* request);

void
grilio_request_unref(
    GRilIoRequest* request);

void
grilio_request_set_blocking(
    GRilIoRequest* request,
    gboolean blocking);

void
grilio_request_set_timeout(
    GRilIoRequest* request,
    int milliseconds);

void
grilio_request_set_retry(
    GRilIoRequest* request,
    guint milliseconds,
    int max_retries);

void
grilio_request_set_retry_func(
    GRilIoRequest* request,
    GRilIoRequestRetryFunc retry);

int
grilio_request_retry_count(
    GRilIoRequest* request);

GRILIO_REQUEST_STATUS
grilio_request_status(
    GRilIoRequest* request);

guint
grilio_request_id(
    GRilIoRequest* request);

/* Since 1.0.25 */
guint
grilio_request_serial(
    GRilIoRequest* request);

/* Building the request */

void
grilio_request_append_byte(
    GRilIoRequest* request,
    guchar value);

void
grilio_request_append_bytes(
    GRilIoRequest* request,
    const void* data,
    guint len);

void
grilio_request_append_int32(
    GRilIoRequest* request,
    guint32 value);

void
grilio_request_append_int32_array(
    GRilIoRequest* request,
    const gint32* values,
    guint count);

void
grilio_request_append_uint32_array(
    GRilIoRequest* request,
    const guint32* values,
    guint count);

void
grilio_request_append_utf8(
    GRilIoRequest* request,
    const char* utf8);

void
grilio_request_append_utf8_chars(
    GRilIoRequest* request,
    const char* utf8,
    gssize num_bytes);

void
grilio_request_append_format(
    GRilIoRequest* request,
    const char* format,
    ...) G_GNUC_PRINTF(2,3);

void
grilio_request_append_format_va(
    GRilIoRequest* request,
    const char* format,
    va_list va);

const void*
grilio_request_data(
    GRilIoRequest* request);

guint
grilio_request_size(
    GRilIoRequest* request);

G_END_DECLS

#endif /* GRILIO_REQUEST_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
