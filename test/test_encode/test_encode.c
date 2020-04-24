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

#include "grilio_encode.h"

/*==========================================================================*
 * Byte
 *==========================================================================*/

static
void
test_byte(
    void)
{
    static const guint8 b[2] = { 0x01, 0x02 };
    GByteArray* a = NULL;

    g_assert((a = grilio_encode_byte(a, b[0])) != NULL);
    g_assert(grilio_encode_byte(a, b[1]) == a);

    g_assert(a->len == 2);
    g_assert(!memcmp(a->data, b, a->len));
    g_byte_array_unref(a);
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
    GByteArray* a = NULL;

    g_assert(!grilio_encode_bytes(a, NULL, 0));
    g_assert((a = grilio_encode_bytes(a, b1, sizeof(b1))) != NULL);
    g_assert(grilio_encode_bytes(a, b2, sizeof(b2)) == a);

    g_assert(a->len == (sizeof(b1) + sizeof(b2)));
    g_assert(!memcmp(a->data, b1, sizeof(b1)));
    g_assert(!memcmp(a->data + sizeof(b1), b2, sizeof(b2)));
    g_byte_array_unref(a);
}

/*==========================================================================*
 * Int32
 *==========================================================================*/

static
void
test_int32(
    void)
{
    static const guint32 i[] = { 1, 2 };
    GByteArray* a = NULL;

    g_assert((a = grilio_encode_int32(a, i[0])) != NULL);
    g_assert(grilio_encode_int32(a, i[1]) == a);

    g_assert(a->len == sizeof(i));
    g_assert(!memcmp(a->data, i, a->len));
    g_byte_array_unref(a);
}

/*==========================================================================*
 * Arrays
 *==========================================================================*/

static
void
test_arrays(
    void)
{
    static const gint32 i1[] = { 1, 2 };
    static const gint32 i2[] = { 3, 4, 5 };
    GByteArray* a = NULL;

    g_assert(!grilio_encode_int32_values(a, NULL, 0));
    g_assert((a = grilio_encode_int32_values(a, i1, G_N_ELEMENTS(i1))) != NULL);
    g_assert(grilio_encode_int32_values(a, i2, G_N_ELEMENTS(i2)) == a);

    g_assert(a->len == (sizeof(i1) + sizeof(i2)));
    g_assert(!memcmp(a->data, i1, sizeof(i1)));
    g_assert(!memcmp(a->data + sizeof(i1), i2, sizeof(i2)));
    g_byte_array_unref(a);
}

/*==========================================================================*
 * String
 *==========================================================================*/

#define UNICHAR(c) TEST_INT16_BYTES(c)

static
void
test_strings(
    void)
{
    const char* str1 = "";
    const char* str2 = "1";
    static const char* str[] = {
        "12", "123", "1234",
        "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82",
        "\xFF", /* invalid */
    };
    static const guchar encoded[] = {
        /* NULL */
        TEST_INT32_BYTES(-1),
        /* "" */
        TEST_INT32_BYTES(0),
        0x00, 0x00, 0xff, 0xff,
        /* "1" */
        TEST_INT32_BYTES(1),
        UNICHAR('1'), 0x00, 0x00,
        /* "12" */
        TEST_INT32_BYTES(2),
        UNICHAR('1'), UNICHAR('2'), 0x00, 0x00, 0x00, 0x00,
        /* "123" */
        TEST_INT32_BYTES(3),
        UNICHAR('1'), UNICHAR('2'), UNICHAR('3'), 0x00, 0x00,
        /* "1234" */
        TEST_INT32_BYTES(4),
        UNICHAR('1'), UNICHAR('2'), UNICHAR('3'), UNICHAR('4'),
        0x00, 0x00, 0x00, 0x00,
        /* "test" in Russian */
        TEST_INT32_BYTES(4),
        UNICHAR(0x0442), UNICHAR(0x0435),
        UNICHAR(0x0441), UNICHAR(0x0442),
        0x00, 0x00, 0x00, 0x00,
        /* Invalid, encoded as an empty string */
        TEST_INT32_BYTES(0),
        0x00, 0x00, 0xff, 0xff,
    };

    GByteArray* a1 = NULL;
    GByteArray* a2 = NULL;
    GByteArray* a3 = NULL;
    guint skip2, skip3;
    guint i;

    /* A few special cases */
    g_assert((a1 = grilio_encode_utf8(a1, NULL)) != NULL);
    skip2 = a1->len;
    g_assert(grilio_encode_utf8(a1, str1) == a1);
    g_assert((a2 = grilio_encode_utf8_chars(a2, str1, -1)) != NULL);
    skip3 = a1->len;
    g_assert(grilio_encode_utf8(a1, str2) == a1);
    g_assert(grilio_encode_utf8_chars(a2, str2, -1) == a2);
    g_assert((a3 = grilio_encode_utf8_chars(a3, str2, -1)) != NULL);

    /* Followed by normal strings */
    for (i = 0; i < G_N_ELEMENTS(str); i++) {
        g_assert(grilio_encode_utf8(a1, str[i]) == a1);
        g_assert(grilio_encode_utf8_chars(a2, str[i], -1) == a2);
        g_assert(grilio_encode_utf8_chars(a3, str[i], strlen(str[i])) == a3);
    }

    g_assert(a1->len == sizeof(encoded));
    g_assert(a2->len == a1->len - skip2);
    g_assert(a3->len == a1->len - skip3);
    g_assert(!memcmp(a1->data, encoded, sizeof(encoded)));
    g_assert(!memcmp(a2->data, encoded + skip2, a2->len));
    g_assert(!memcmp(a3->data, encoded + skip3, a3->len));
    g_byte_array_unref(a1);
    g_byte_array_unref(a2);
    g_byte_array_unref(a3);
}

/*==========================================================================*
 * Format
 *==========================================================================*/

static
void
test_format(
    void)
{
    static const guchar encoded[] = {
        TEST_INT32_BYTES(5),
        UNICHAR('t'),
        UNICHAR('e'),
        UNICHAR('s'),
        UNICHAR('t'),
        UNICHAR('1'),
        0x00, 0x00
    };

    GByteArray* a = grilio_encode_format(NULL, "%s1", "test");

    g_assert(a->len == sizeof(encoded));
    g_assert(!memcmp(a->data, encoded, sizeof(encoded)));
    g_byte_array_unref(a);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/encode/"

int main(int argc, char* argv[])
{
    TestOpt test_opt;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "Byte", test_byte);
    g_test_add_func(TEST_PREFIX "Bytes", test_bytes);
    g_test_add_func(TEST_PREFIX "Int32", test_int32);
    g_test_add_func(TEST_PREFIX "Arrays", test_arrays);
    g_test_add_func(TEST_PREFIX "Strings", test_strings);
    g_test_add_func(TEST_PREFIX "Format", test_format);
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
