/*
 * Copyright (C) 2018 Jolla Ltd.
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

#include "test_common.h"

#include "grilio_p.h"
#include "grilio_request.h"

/*==========================================================================*
 * Basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    /* NULL tolerance */
    g_assert(!grilio_request_ref(NULL));
    grilio_request_unref(NULL);
    grilio_request_set_blocking(NULL, FALSE);
    grilio_request_set_timeout(NULL, FALSE);
    grilio_request_set_retry(NULL, 0, 0);
    grilio_request_set_retry_func(NULL, NULL);
    g_assert(!grilio_request_retry_count(NULL));
    g_assert(grilio_request_status(NULL) == GRILIO_REQUEST_INVALID);
    g_assert(!grilio_request_id(NULL));
    g_assert(!grilio_request_serial(NULL));
    grilio_request_append_byte(NULL, 0);
    grilio_request_append_bytes(NULL, NULL, 0);
    grilio_request_append_utf8(NULL, NULL);
    grilio_request_append_utf8_chars(NULL, NULL, 0);
    grilio_request_append_int32(NULL, 0);
    grilio_request_append_int32_array(NULL, NULL, 0);
    grilio_request_append_uint32_array(NULL, NULL, 0);
    grilio_request_append_format(NULL, NULL);
    g_assert(!grilio_request_data(NULL));
    g_assert(!grilio_request_size(NULL));
}

/*==========================================================================*
 * Flags
 *==========================================================================*/

static
void
test_flags(
    void)
{
    GRilIoRequest* req = grilio_request_new();

    g_assert(!(req->flags & GRILIO_REQUEST_FLAG_BLOCKING));
    grilio_request_set_blocking(req, TRUE);
    g_assert(req->flags & GRILIO_REQUEST_FLAG_BLOCKING);
    grilio_request_set_blocking(req, FALSE);
    g_assert(!(req->flags & GRILIO_REQUEST_FLAG_BLOCKING));

    grilio_request_unref(req);
}

/*==========================================================================*
 * Byte
 *==========================================================================*/

static
void
test_byte(
    void)
{
    static const guint8 b1[] = { 0x01, 0x02 };
    static const guint8 b2[] = { 0x03, 0x04, 0x05 };
    GRilIoRequest* r1 = grilio_request_new();
    GRilIoRequest* r2 = grilio_request_sized_new(1);
    int i;

    for (i = 0; i < G_N_ELEMENTS(b1); i++) {
        grilio_request_append_byte(r1, b1[i]);
    }

    for (i = 0; i < G_N_ELEMENTS(b2); i++) {
        grilio_request_append_byte(r2, b2[i]);
    }

    g_assert(grilio_request_size(r1) == sizeof(b1));
    g_assert(grilio_request_size(r2) == sizeof(b2));
    g_assert(!memcmp(grilio_request_data(r1), b1, sizeof(b1)));
    g_assert(!memcmp(grilio_request_data(r2), b2, sizeof(b2)));

    grilio_request_unref(r1);
    grilio_request_unref(r2);
}

/*==========================================================================*
 * Bytes
 *==========================================================================*/

static
void
test_bytes(
    void)
{
    static const guint8 b1[] = { 0x01, 0x02 };
    static const guint8 b2[] = { 0x03, 0x04, 0x05 };
    GRilIoRequest* r1 = grilio_request_new();
    GRilIoRequest* r2 = grilio_request_sized_new(2);

    grilio_request_append_bytes(r1, b1, sizeof(b1));
    grilio_request_append_bytes(r2, b2, sizeof(b2));

    g_assert(grilio_request_size(r1) == sizeof(b1));
    g_assert(grilio_request_size(r2) == sizeof(b2));
    g_assert(!memcmp(grilio_request_data(r1), b1, sizeof(b1)));
    g_assert(!memcmp(grilio_request_data(r2), b2, sizeof(b2)));

    grilio_request_unref(r1);
    grilio_request_unref(r2);
}

/*==========================================================================*
 * Arrays
 *==========================================================================*/

static
void
test_arrays(
    void)
{
    static const gint32 i1[] = { -1, 0, 1 };
    static const guint32 i2[] = { 0xffffffff, 0, 1 };
    GRilIoRequest* r1 = grilio_request_new();
    GRilIoRequest* r2 = grilio_request_new();

    grilio_request_append_int32_array(r1, i1, G_N_ELEMENTS(i1));
    grilio_request_append_uint32_array(r2, i2, G_N_ELEMENTS(i2));

    g_assert(grilio_request_size(r1) == sizeof(i1));
    g_assert(grilio_request_size(r2) == sizeof(i2));
    g_assert(!memcmp(grilio_request_data(r1), i1, sizeof(i1)));
    g_assert(!memcmp(grilio_request_data(r2), i2, sizeof(i2)));

    grilio_request_unref(r1);
    grilio_request_unref(r2);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/request/"

int main(int argc, char* argv[])
{
    TestOpt test_opt;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "Basic", test_basic);
    g_test_add_func(TEST_PREFIX "Flags", test_flags);
    g_test_add_func(TEST_PREFIX "Byte", test_byte);
    g_test_add_func(TEST_PREFIX "Bytes", test_bytes);
    g_test_add_func(TEST_PREFIX "Arrays", test_arrays);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
