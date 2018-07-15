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

#include "test_common.h"

#include "grilio_p.h"
#include "grilio_parser.h"

#include <gutil_log.h>

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#  define UNICHAR2(high,low) low, high
#elif G_BYTE_ORDER == G_BIG_ENDIAN
#  define UNICHAR2(high,low) high, low
#endif
#define UNICHAR1(c) UNICHAR2(0,c)

static
void
test_parser_init_req(
    GRilIoParser* p,
    GRilIoRequest* req)
{
    grilio_parser_init(p, grilio_request_data(req), grilio_request_size(req));
}

/*==========================================================================*
 * BasicTypes
 *==========================================================================*/

static
void
test_basic_types(
    void)
{
    const static gint32 test_i32 = -1234;
    const static guint32 test_u32 = 0x01020304;
    const static guchar test_bytes[4] = { 0x05, 0x06, 0x07, 0x08 };
    const static gint32 test_ints[3] = { 9, 10, 11 };
    GRilIoParser parser;
    GRilIoRequest* req = grilio_request_sized_new(12);
    GRilIoRequest* req2 = grilio_request_new();
    const void* data;
    guint len;
    gint32 i32 = 0;
    guint32 u32 = 0;
    guchar bytes[4];
    gint32 ints[3];

    grilio_request_append_int32(req, test_i32);
    grilio_request_append_int32(req, test_u32);
    grilio_request_append_byte(req, test_bytes[0]);
    grilio_request_append_byte(req, test_bytes[1]);
    grilio_request_append_byte(req, test_bytes[2]);
    grilio_request_append_byte(req, test_bytes[3]);
    grilio_request_append_int32_array(req, test_ints, 3);
    data = grilio_request_data(req);
    len = grilio_request_size(req);

    g_assert(grilio_request_status(req) == GRILIO_REQUEST_NEW);
    g_assert(grilio_request_id(req) == 0);
    g_assert(len == 24);

    memset(bytes, 0, sizeof(bytes));
    memset(ints, 0, sizeof(ints));

    /* Parse what we have just encoded */
    grilio_parser_init(&parser, data, len);
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(grilio_parser_get_uint32(&parser, &u32));
    g_assert(grilio_parser_get_byte(&parser, bytes));
    g_assert(grilio_parser_get_byte(&parser, bytes + 1));
    g_assert(grilio_parser_get_byte(&parser, bytes + 2));
    g_assert(grilio_parser_get_byte(&parser, bytes + 3));
    g_assert(grilio_parser_get_int32_array(&parser, ints, 3));
    g_assert(i32 == test_i32);
    g_assert(u32 == test_u32);
    g_assert(!memcmp(bytes, test_bytes, sizeof(bytes)));
    g_assert(!memcmp(ints, test_ints, sizeof(ints)));
    g_assert(grilio_parser_at_end(&parser));

    /* Parse is again, without checking the values */
    grilio_parser_init(&parser, data, len);
    g_assert(grilio_parser_get_int32(&parser, NULL));
    g_assert(grilio_parser_get_uint32(&parser, NULL));
    g_assert(grilio_parser_get_byte(&parser, NULL));
    g_assert(grilio_parser_get_byte(&parser, NULL));
    g_assert(grilio_parser_get_byte(&parser, NULL));
    g_assert(grilio_parser_get_byte(&parser, NULL));
    g_assert(grilio_parser_get_int32_array(&parser, NULL, 3));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(!grilio_parser_get_uint32(&parser, NULL));
    g_assert(!grilio_parser_get_byte(&parser, NULL));
    g_assert(!grilio_parser_get_utf8(&parser));
    g_assert(!grilio_parser_get_int32_array(&parser, ints, 1));
    g_assert(!grilio_parser_get_int32_array(&parser, NULL, 1));
    g_assert(!grilio_parser_skip_string(&parser));

    /* These don't do anything */
    grilio_request_append_bytes(req2, NULL, 0);
    grilio_request_append_bytes(req2, &bytes, 0);
    grilio_request_append_bytes(req2, NULL, 1);

    /* All these function should tolerate NULL arguments */
    grilio_request_set_timeout(NULL, 0);
    grilio_request_unref(NULL);
    grilio_request_append_int32(NULL, 0);
    grilio_request_append_byte(NULL, 0);
    grilio_request_append_bytes(NULL, NULL, 0);
    grilio_request_append_bytes(NULL, &bytes, 0);
    grilio_request_append_bytes(NULL, NULL, 1);
    grilio_request_append_utf8(NULL, NULL);
    grilio_request_append_int32_array(NULL, NULL, 0);
    grilio_request_append_uint32_array(NULL, NULL, 0);
    g_assert(!grilio_request_ref(NULL));
    g_assert(grilio_request_status(NULL) == GRILIO_REQUEST_INVALID);
    g_assert(!grilio_request_id(NULL));
    g_assert(!grilio_request_data(NULL));
    g_assert(!grilio_request_size(NULL));

    grilio_request_append_bytes(req2, data, len);
    g_assert(grilio_request_data(req2));
    g_assert(len == grilio_request_size(req2));
    g_assert(!memcmp(data, grilio_request_data(req2), len));

    grilio_request_unref(req);
    grilio_request_unref(req2);
}

/*==========================================================================*
 * Strings
 *==========================================================================*/

static
void
test_strings(
    void)
{
    static const char* test_string[] = {
        NULL, "", "1", "12", "123", "1234",
        "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82"
    };
    static const guchar valid_data[] = {
        /* NULL */
        0xff, 0xff, 0xff, 0xff,
        /* "" */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xff, 0xff,
        /* "1" */
        0x01, 0x00, 0x00, 0x00,
        UNICHAR1('1'), 0x00, 0x00,
        /* "12" */
        0x02, 0x00, 0x00, 0x00,
        UNICHAR1('1'), UNICHAR1('2'), 0x00, 0x00, 0x00, 0x00,
        /* "123" */
        0x03, 0x00, 0x00, 0x00,
        UNICHAR1('1'), UNICHAR1('2'), UNICHAR1('3'), 0x00, 0x00,
        /* "1234" */
        0x04, 0x00,0x00, 0x00,
        UNICHAR1('1'), UNICHAR1('2'), UNICHAR1('3'), UNICHAR1('4'),
        0x00, 0x00, 0x00, 0x00,
        /* "test" in Russian */
        0x04, 0x00,0x00, 0x00,
        UNICHAR2(0x04,0x42), UNICHAR2(0x04,0x35),
        UNICHAR2(0x04,0x41), UNICHAR2(0x04,0x42),
        0x00, 0x00, 0x00, 0x00
    };

    char* decoded[G_N_ELEMENTS(test_string)];
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    guint i;

    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        grilio_request_append_utf8_chars(req, test_string[i], -1);
    }

    GVERBOSE("Encoded %u bytes", grilio_request_size(req));
    test_parser_init_req(&parser, req);

    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        GRilIoParser parser2 = parser;
        g_assert(grilio_parser_get_nullable_utf8(&parser2, NULL));
        decoded[i] = grilio_parser_get_utf8(&parser);
    }

    /* Decode */
    GASSERT(grilio_parser_at_end(&parser));
    GASSERT(grilio_request_size(req) == sizeof(valid_data));
    GASSERT(!memcmp(valid_data, grilio_request_data(req), sizeof(valid_data)));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(grilio_request_size(req) == sizeof(valid_data));
    g_assert(!memcmp(valid_data, grilio_request_data(req), sizeof(valid_data)));

    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        if (!test_string[i]) {
            g_assert(!decoded[i]);
        } else {
            g_assert(decoded[i]);
            g_assert(!strcmp(decoded[i], test_string[i]));
        }
    }

    /* Skip */
    test_parser_init_req(&parser, req);
    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        grilio_parser_skip_string(&parser);
        g_free(decoded[i]);
    }
    g_assert(grilio_parser_at_end(&parser));
    grilio_request_unref(req);
}

/*==========================================================================*
 * Split
 *==========================================================================*/

static
void
test_split(
    void)
{
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    char** out;

    grilio_request_append_utf8(req, "\xD1\x85\xD1\x83\xD0\xB9 123");
    GVERBOSE("Encoded %u bytes", grilio_request_size(req));

    test_parser_init_req(&parser, req);
    out = grilio_parser_split_utf8(&parser, " ");
    g_assert(out);
    g_assert(g_strv_length(out) == 2);
    g_assert(!grilio_parser_split_utf8(&parser, " "));
    g_assert(g_utf8_strlen(out[0], -1) == 3);
    g_assert(!strcmp(out[1], "123"));

    g_strfreev(out);
    grilio_request_unref(req);
}

/*==========================================================================*
 * Broken
 *==========================================================================*/

static
void
test_broken(
    void)
{
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    guint32 badlen = GINT32_TO_BE(-2);

    grilio_request_append_utf8(req, "1234");
    GVERBOSE("Encoded %u bytes", grilio_request_size(req));

    grilio_parser_init(&parser, grilio_request_data(req),
        grilio_request_size(req) - 2);
    g_assert(!grilio_parser_skip_string(&parser));
    g_assert(!grilio_parser_get_utf8(&parser));

    grilio_parser_init(&parser, grilio_request_data(req), 3);
    g_assert(!grilio_parser_skip_string(&parser));
    g_assert(!grilio_parser_get_utf8(&parser));

    grilio_parser_init(&parser, &badlen, sizeof(badlen));
    g_assert(!grilio_parser_skip_string(&parser));
    g_assert(!grilio_parser_get_utf8(&parser));

    grilio_request_unref(req);
}

/*==========================================================================*
 * InvalidUtf8
 *==========================================================================*/

static
void
test_invalid_utf8(
    void)
{
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    /* Valid UTF8 character followed by an invalid one */
    const char* in = "\xD1\x82\x81";
    char* out;

    grilio_request_append_utf8(req, in);
    GVERBOSE("Encoded %u bytes", grilio_request_size(req));

    test_parser_init_req(&parser, req);

    /* Invalid tail is dropped by grilio_request_append_utf8 */
    out = grilio_parser_get_utf8(&parser);
    g_assert(out);
    g_assert(strlen(out) == 2);
    g_assert(!memcmp(in, out, strlen(out)));
    g_free(out);

    g_assert(grilio_parser_at_end(&parser));
    grilio_request_unref(req);
}

/*==========================================================================*
 * ArrayUtf8
 *==========================================================================*/

static
void
test_array_utf8(
    void)
{
    GRilIoRequest* req0 = grilio_request_array_utf8_new(0, NULL);
    GRilIoRequest* req1 = grilio_request_array_utf8_new(1, NULL);
    GRilIoRequest* req2 = grilio_request_array_utf8_new(2, "1", "2");
    GRilIoParser parser;
    char* str;
    gint32 i32 = 0;

    g_assert(grilio_request_size(req0) == 4);
    g_assert(grilio_request_size(req1) == 8);
    g_assert(grilio_request_size(req2) == 20);

    test_parser_init_req(&parser, req0);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(i32 == 0);

    test_parser_init_req(&parser, req1);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(i32 == 1);
    g_assert(!grilio_parser_get_utf8(&parser));
    g_assert(grilio_parser_at_end(&parser));

    test_parser_init_req(&parser, req2);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(i32 == 2);
    str = grilio_parser_get_utf8(&parser);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(!g_strcmp0(str, "1"));
    g_free(str);
    str = grilio_parser_get_utf8(&parser);
    g_assert(grilio_parser_at_end(&parser));
    g_assert(!g_strcmp0(str, "2"));
    g_free(str);

    grilio_request_unref(req0);
    grilio_request_unref(req1);
    grilio_request_unref(req2);
}

/*==========================================================================*
 * ArrayInt32
 *==========================================================================*/

static
void
test_array_int32(
    void)
{
    GRilIoRequest* req0 = grilio_request_array_int32_new(0, 0);
    GRilIoRequest* req1 = grilio_request_array_int32_new(1, 0);
    GRilIoRequest* req2 = grilio_request_array_int32_new(2, 1, 2);
    GRilIoParser parser;
    gint32 i32 = 0;

    g_assert(grilio_request_size(req0) == 4);
    g_assert(grilio_request_size(req1) == 8);
    g_assert(grilio_request_size(req2) == 12);

    test_parser_init_req(&parser, req0);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(i32 == 0);

    test_parser_init_req(&parser, req1);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(i32 == 1);
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(i32 == 0);

    test_parser_init_req(&parser, req2);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(i32 == 2);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(i32 == 1);
    g_assert(!grilio_parser_at_end(&parser));
    g_assert(grilio_parser_get_int32(&parser, &i32));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(i32 == 2);

    grilio_request_unref(req0);
    grilio_request_unref(req1);
    grilio_request_unref(req2);
}

/*==========================================================================*
 * Format
 *==========================================================================*/

static
void
test_format(
    void)
{
    const char* formatted_string = "1234";
    GRilIoRequest* req1 = grilio_request_new();
    GRilIoRequest* req2 = grilio_request_new();
    char* decoded;
    GRilIoParser parser;

    grilio_request_append_utf8(req1, formatted_string);
    grilio_request_append_format(req2, "%d%s", 12, "34");

    g_assert(grilio_request_size(req1) == grilio_request_size(req2));
    grilio_parser_init(&parser, grilio_request_data(req2),
        grilio_request_size(req2));

    decoded = grilio_parser_get_utf8(&parser);
    g_assert(decoded);
    g_assert(grilio_parser_at_end(&parser));
    g_assert(!g_strcmp0(decoded, formatted_string));

    g_free(decoded);
    grilio_request_unref(req1);
    grilio_request_unref(req2);
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
 * SubParser
 *==========================================================================*/

static
void
test_subparser(
    void)
{
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser p1, p2;
    gint32 i32 = 0;

    grilio_request_append_int32(req, 1);
    grilio_request_append_int32(req, 2);
    test_parser_init_req(&p1, req);
    g_assert(grilio_parser_bytes_remaining(&p1) == 8);
    g_assert(grilio_parser_get_data(&p1, &p2, 4) == 4);

    /* First int has been moved to p2 */
    g_assert(grilio_parser_bytes_remaining(&p1) == 4);
    g_assert(grilio_parser_bytes_remaining(&p2) == 4);
    g_assert(grilio_parser_get_int32(&p1, &i32));
    g_assert(i32 == 2);
    g_assert(grilio_parser_get_int32(&p2, &i32));
    g_assert(i32 == 1);
    g_assert(grilio_parser_at_end(&p1));
    g_assert(grilio_parser_at_end(&p2));
    g_assert(!grilio_parser_get_data(&p1, &p2, 1));

    grilio_request_unref(req);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/parsel/"

int main(int argc, char* argv[])
{
    TestOpt test_opt;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "BasicTypes", test_basic_types);
    g_test_add_func(TEST_PREFIX "Strings", test_strings);
    g_test_add_func(TEST_PREFIX "Split", test_split);
    g_test_add_func(TEST_PREFIX "Broken", test_broken);
    g_test_add_func(TEST_PREFIX "InvalidUtf8", test_invalid_utf8);
    g_test_add_func(TEST_PREFIX "ArrayUtf8", test_array_utf8);
    g_test_add_func(TEST_PREFIX "ArrayInt32", test_array_int32);
    g_test_add_func(TEST_PREFIX "Format", test_format);
    g_test_add_func(TEST_PREFIX "Flags", test_flags);
    g_test_add_func(TEST_PREFIX "SubParser", test_subparser);
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
