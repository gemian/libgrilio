/*
 * Copyright (C) 2015-2016 Jolla Ltd.
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
 *   3. Neither the name of the Jolla Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
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

#include "grilio_request.h"
#include "grilio_channel.h"
#include "grilio_parser.h"

#include <gutil_log.h>

#define RET_OK       (0)
#define RET_ERR      (1)

#define RIL_REQUEST_BASEBAND_VERSION 51
#define RIL_UNSOL_RIL_CONNECTED 1034

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#  define UNICHAR2(c) c, 0
#elif G_BYTE_ORDER == G_BIG_ENDIAN
#  define UNICHAR2(c) 0, c
#endif

typedef struct test_desc {
    const char* name;
    int (*run)();
} TestDesc;

/*==========================================================================*
 * BasicTypes
 *==========================================================================*/

static
int
test_basic_types()
{
    int ret = RET_ERR;
    const static gint32 test_i32 = -1234;
    const static guint32 test_u32 = 0x01020304;
    const static guchar test_bytes[4] = { 0x05, 0x06, 0x07, 0x08 };
    const static gint32 test_ints[3] = { 9, 10, 11 };
    GRilIoRequest* req = grilio_request_sized_new(12);
    GRilIoRequest* req2 = grilio_request_new();
    const void* data;
    guint len;

    grilio_request_append_int32(req, test_i32);
    grilio_request_append_int32(req, test_u32);
    grilio_request_append_byte(req, test_bytes[0]);
    grilio_request_append_byte(req, test_bytes[1]);
    grilio_request_append_byte(req, test_bytes[2]);
    grilio_request_append_byte(req, test_bytes[3]);
    grilio_request_append_int32_array(req, test_ints, 3);
    data = grilio_request_data(req);
    len = grilio_request_size(req);

    if (grilio_request_status(req) == GRILIO_REQUEST_NEW &&
        grilio_request_id(req) == 0 &&
        len == 24) {
        GRilIoParser parser;
        gint32 i32 = 0;
        guint32 u32 = 0;
        guchar bytes[4];
        gint32 ints[3];
        memset(bytes, 0, sizeof(bytes));
        memset(ints, 0, sizeof(ints));
        /* Parse what we have just encoded */
        grilio_parser_init(&parser, data, len);
        if (grilio_parser_get_int32(&parser, &i32) &&
            grilio_parser_get_uint32(&parser, &u32) &&
            grilio_parser_get_byte(&parser, bytes) &&
            grilio_parser_get_byte(&parser, bytes + 1) &&
            grilio_parser_get_byte(&parser, bytes + 2) &&
            grilio_parser_get_byte(&parser, bytes + 3) &&
            grilio_parser_get_int32_array(&parser, ints, 3) &&
            i32 == test_i32 &&
            u32 == test_u32 &&
            bytes[0] == test_bytes[0] &&
            bytes[1] == test_bytes[1] &&
            bytes[2] == test_bytes[2] &&
            bytes[3] == test_bytes[3] &&
            !memcmp(ints, test_ints, sizeof(ints)) &&
            grilio_parser_at_end(&parser)) {
            /* Parse is again, without checking the values */
            grilio_parser_init(&parser, data, len);
            if (grilio_parser_get_int32(&parser, NULL) &&
                grilio_parser_get_uint32(&parser, NULL) &&
                grilio_parser_get_byte(&parser, NULL) &&
                grilio_parser_get_byte(&parser, NULL) &&
                grilio_parser_get_byte(&parser, NULL) &&
                grilio_parser_get_byte(&parser, NULL) &&
                grilio_parser_get_int32_array(&parser, NULL, 3) &&
                grilio_parser_at_end(&parser) &&
                !grilio_parser_get_uint32(&parser, NULL) &&
                !grilio_parser_get_byte(&parser, NULL) &&
                !grilio_parser_get_utf8(&parser) &&
                !grilio_parser_get_int32_array(&parser, ints, 1) &&
                !grilio_parser_get_int32_array(&parser, NULL, 1) &&
                !grilio_parser_skip_string(&parser)) {
                ret = RET_OK;
            }
        }
    }

    /* These don't do anything */
    grilio_request_append_bytes(req2, NULL, 0);
    grilio_request_append_bytes(req2, &ret, 0);
    grilio_request_append_bytes(req2, NULL, 1);

    /* All these function shoulf tolerate NULL arguments */
    grilio_request_set_timeout(NULL, 0);
    grilio_request_unref(NULL);
    grilio_request_append_int32(NULL, 0);
    grilio_request_append_byte(NULL, 0);
    grilio_request_append_bytes(NULL, NULL, 0);
    grilio_request_append_bytes(NULL, &ret, 0);
    grilio_request_append_bytes(NULL, NULL, 1);
    grilio_request_append_utf8(NULL, NULL);
    grilio_request_append_int32_array(NULL, NULL, 0);
    grilio_request_append_uint32_array(NULL, NULL, 0);
    if (grilio_request_ref(NULL) ||
        grilio_request_status(NULL) != GRILIO_REQUEST_INVALID ||
        grilio_request_id(NULL) != 0 ||
        grilio_request_data(NULL) ||
        grilio_request_size(NULL)) {
        ret = RET_ERR;
    }

    grilio_request_append_bytes(req2, data, len);
    if (!grilio_request_data(req2) ||
        len != grilio_request_size(req2) ||
        memcmp(data, grilio_request_data(req2), len)) {
        ret = RET_ERR;
    }

    grilio_request_unref(req);
    grilio_request_unref(req2);
    return ret;
}

/*==========================================================================*
 * Strings
 *==========================================================================*/

static
int
test_strings()
{
    static const char* test_string[] = { NULL, "", "1", "12", "123", "1234" };
    static const guchar valid_data[] = {
        /* NULL */
        0xff, 0xff, 0xff, 0xff,
        /* "" */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xff, 0xff,
        /* "1" */
        0x01, 0x00, 0x00, 0x00,
        UNICHAR2('1'), 0x00, 0x00,
        /* "12" */
        0x02, 0x00, 0x00, 0x00,
        UNICHAR2('1'), UNICHAR2('2'), 0x00, 0x00, 0x00, 0x00,
        /* "123" */
        0x03, 0x00, 0x00, 0x00,
        UNICHAR2('1'), UNICHAR2('2'), UNICHAR2('3'), 0x00, 0x00,
        /* "1234" */
        0x04, 0x00,0x00, 0x00,
        UNICHAR2('1'), UNICHAR2('2'), UNICHAR2('3'), UNICHAR2('4'),
        0x00, 0x00, 0x00, 0x00
    };

    int ret = RET_ERR;
    char* decoded[G_N_ELEMENTS(test_string)];
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    guint i;

    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        grilio_request_append_utf8_chars(req, test_string[i], -1);
    }

    GVERBOSE("Encoded %u bytes", grilio_request_size(req));
    grilio_parser_init(&parser, grilio_request_data(req),
        grilio_request_size(req));

    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        decoded[i] = grilio_parser_get_utf8(&parser);
    }

    /* Decode */
    GASSERT(grilio_parser_at_end(&parser));
    GASSERT(grilio_request_size(req) == sizeof(valid_data));
    GASSERT(!memcmp(valid_data, grilio_request_data(req), sizeof(valid_data)));
    if (grilio_parser_at_end(&parser) &&
        grilio_request_size(req) == sizeof(valid_data) &&
        !memcmp(valid_data, grilio_request_data(req), sizeof(valid_data))) {
        ret = RET_OK;

        for (i=0; i<G_N_ELEMENTS(test_string); i++) {
            if (!test_string[i]) {
                GASSERT(!decoded[i]);
                if (decoded[i]) {
                    ret = RET_ERR;
                }
            } else {
                GASSERT(decoded[i]);
                if (decoded[i]) {
                    GASSERT(!strcmp(decoded[i], test_string[i]));
                    if (strcmp(decoded[i], test_string[i])) {
                        ret = RET_ERR;
                    }
                } else {
                    ret = RET_ERR;
                }
            }
        }
    }

    /* Skip */
    grilio_parser_init(&parser, grilio_request_data(req),
        grilio_request_size(req));
    for (i=0; i<G_N_ELEMENTS(test_string); i++) {
        grilio_parser_skip_string(&parser);
        g_free(decoded[i]);
    }
    GASSERT(grilio_parser_at_end(&parser));
    if (!grilio_parser_at_end(&parser)) {
        ret = RET_ERR;
    }

    grilio_request_unref(req);
    return ret;
}

/*==========================================================================*
 * Split
 *==========================================================================*/

static
int
test_split()
{
    int ret = RET_ERR;
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;
    char** out;

    grilio_request_append_utf8(req, "\xD1\x85\xD1\x83\xD0\xB9 123");
    GVERBOSE("Encoded %u bytes", grilio_request_size(req));

    grilio_parser_init(&parser, grilio_request_data(req),
        grilio_request_size(req));
    out = grilio_parser_split_utf8(&parser, " ");
    if (out &&
        g_strv_length(out) == 2 &&
        !grilio_parser_split_utf8(&parser, " ") &&
        g_utf8_strlen(out[0], -1) == 3 &&
        strcmp(out[1], "123") == 0) {
        ret = RET_OK;
    }

    g_strfreev(out);
    grilio_request_unref(req);
    return ret;
}

/*==========================================================================*
 * Broken
 *==========================================================================*/

static
int
test_broken()
{
    int ret = RET_ERR;
    GRilIoRequest* req = grilio_request_new();
    GRilIoParser parser;

    grilio_request_append_utf8(req, "1234");
    GVERBOSE("Encoded %u bytes", grilio_request_size(req));

    grilio_parser_init(&parser, grilio_request_data(req),
        grilio_request_size(req) - 2);
    if (!grilio_parser_skip_string(&parser) &&
        !grilio_parser_get_utf8(&parser)) {
        grilio_parser_init(&parser, grilio_request_data(req), 3);
        if (!grilio_parser_skip_string(&parser) &&
            !grilio_parser_get_utf8(&parser)) {
            guint32 badlen = GINT32_TO_BE(-2);
            grilio_parser_init(&parser, &badlen, sizeof(badlen));
            if (!grilio_parser_skip_string(&parser) &&
                !grilio_parser_get_utf8(&parser)) {
                ret = RET_OK;
            }
        }
    }

    grilio_request_unref(req);
    return ret;
}

/*==========================================================================*
 * Format
 *==========================================================================*/

static
int
test_format()
{
    int ret = RET_ERR;
    const char* formatted_string = "1234";
    GRilIoRequest* req1 = grilio_request_new();
    GRilIoRequest* req2 = grilio_request_new();

    grilio_request_append_utf8(req1, formatted_string);
    grilio_request_append_format(req2, "%d%s", 12, "34");

    GASSERT(grilio_request_size(req1) == grilio_request_size(req2));
    if (grilio_request_size(req1) == grilio_request_size(req2)) {
        char* decoded;
        GRilIoParser parser;
        grilio_parser_init(&parser, grilio_request_data(req2),
            grilio_request_size(req2));
        decoded = grilio_parser_get_utf8(&parser);
        GASSERT(grilio_parser_at_end(&parser));
        GASSERT(!g_strcmp0(decoded, formatted_string));
        if (grilio_parser_at_end(&parser) &&
            !g_strcmp0(decoded, formatted_string)) {
            ret = RET_OK;
        }
        g_free(decoded);
    }
    grilio_request_unref(req1);
    grilio_request_unref(req2);
    return ret;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static const TestDesc all_tests[] = {
    {
        "BasicTypes",
        test_basic_types
    },{
        "Strings",
        test_strings
    },{
        "Split",
        test_split
    },{
        "Broken",
        test_broken
    },{
        "Format",
        test_format
    }
};

static
int
test_run_once(
    const TestDesc* desc)
{
    int ret = desc->run(desc);
    GINFO("%s: %s", (ret == RET_OK) ? "OK" : "FAILED", desc->name);
    return ret;
}

static
int
test_run(
    const char* name)
{
    int i, ret;
    if (name) {
        const TestDesc* found = NULL;
        for (i=0, ret = RET_ERR; i<G_N_ELEMENTS(all_tests); i++) {
            const TestDesc* test = all_tests + i;
            if (!strcmp(test->name, name)) {
                ret = test_run_once(test);
                found = test;
                break;
            }
        }
        if (!found) GERR("No such test: %s", name);
    } else {
        for (i=0, ret = RET_OK; i<G_N_ELEMENTS(all_tests); i++) {
            int test_status = test_run_once(all_tests + i);
            if (ret == RET_OK && test_status != RET_OK) ret = test_status;
        }
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    GError* error = NULL;
    GOptionContext* options;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { NULL }
    };

    options = g_option_context_new("[TEST]");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (verbose) {
            gutil_log_default.level = GLOG_LEVEL_VERBOSE;
        } else {
            gutil_log_timestamp = FALSE;
            gutil_log_default.level = GLOG_LEVEL_INFO;
            grilio_log.level = GLOG_LEVEL_NONE;
        }
        if (argc < 2) {
            ret = test_run(NULL);
        } else {
            int i;
            for (i=1, ret = RET_OK; i<argc; i++) {
                int test_status =  test_run(argv[i]);
                if (ret == RET_OK && test_status != RET_OK) ret = test_status;
            }
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
        ret = RET_ERR;
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
