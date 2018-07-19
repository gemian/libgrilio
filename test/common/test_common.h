/*
 * Copyright (C) 2017-2018 Jolla Ltd.
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

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <gutil_types.h>

#define TEST_FLAG_DEBUG (0x01)

typedef struct test_opt {
    int flags;
} TestOpt;

/* Should be invoked after g_test_init */
void
test_init(
    TestOpt* opt,
    int argc,
    char* argv[]);

/* Helper macros */

#define TEST_INT16_LE(x) \
    (guint8)(x), (guint8)((x) >> 8)
#define TEST_INT16_BE(x) \
    (guint8)((x) >> 8), (guint8)(x)

#define TEST_INT32_LE(x) \
    (guint8)(x), (guint8)(((guint16)(x)) >> 8), \
    (guint8)(((guint32)(x)) >> 16), (guint8)(((guint32)(x)) >> 24)
#define TEST_INT32_BE(x) \
    (guint8)(((guint32)(x)) >> 24), (guint8)(((guint32)(x)) >> 16), \
    (guint8)(((guint16)(x)) >> 8), (guint8)(x)

#define TEST_INT64_LE(x) \
    (guint8)(x), (guint8)(((guint16)(x)) >> 8), \
    (guint8)(((guint32)(x)) >> 16), (guint8)(((guint32)(x)) >> 24), \
    (guint8)(((guint64)(x)) >> 32), (guint8)(((guint64)(x)) >> 40), \
    (guint8)(((guint64)(x)) >> 48), (guint8)(((guint64)(x)) >> 56)
#define TEST_INT64_BE(x) \
    (guint8)(((guint64)(x)) >> 56), (guint8)(((guint64)(x)) >> 48), \
    (guint8)(((guint64)(x)) >> 40), (guint8)(((guint64)(x)) >> 32), \
    (guint8)(((guint32)(x)) >> 24), (guint8)(((guint32)(x)) >> 16), \
    (guint8)(((guint16)(x)) >> 8), (guint8)(x)

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#  define TEST_INT16_BYTES(x) TEST_INT16_LE(x)
#  define TEST_INT32_BYTES(x) TEST_INT32_LE(x)
#  define TEST_INT64_BYTES(x) TEST_INT64_LE(x)
#elif G_BYTE_ORDER == G_BIG_ENDIAN
#  define TEST_INT16_BYTES(x) TEST_INT16_BE(x)
#  define TEST_INT32_BYTES(x) TEST_INT32_BE(x)
#  define TEST_INT64_BYTES(x) TEST_INT64_BE(x)
#else /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */
#error unknown byte order
#endif /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */

#define TEST_ARRAY_AND_COUNT(a) a, G_N_ELEMENTS(a)
#define TEST_ARRAY_AND_SIZE(a) a, sizeof(a)

#endif /* TEST_COMMON_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
