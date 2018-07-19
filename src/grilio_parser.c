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

#include "grilio_parser.h"
#include "grilio_p.h"

#include <gutil_macros.h>

typedef struct grilio_parser_priv {
    const guchar* ptr;
    const guchar* end;
} GRilIoParserPriv;

G_STATIC_ASSERT(sizeof(GRilIoParser) >= sizeof(GRilIoParserPriv));

static inline GRilIoParserPriv* grilio_parser_cast(GRilIoParser* parser)
    { return (GRilIoParserPriv*)parser; }

void
grilio_parser_init(
    GRilIoParser* parser,
    const void* data,
    gsize len)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    p->ptr = data;
    p->end = p->ptr + len;
}

gboolean
grilio_parser_at_end(
    GRilIoParser* parser)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    return p->ptr >= p->end;
}

gboolean
grilio_parser_get_byte(
    GRilIoParser* parser,
    guchar* value)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    if (p->ptr < p->end) {
        if (value) *value = *p->ptr;
        p->ptr++;
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
grilio_parser_get_int32(
    GRilIoParser* parser,
    gint32* value)
{
    return grilio_parser_get_uint32(parser, (guint32*)value);
}

gboolean
grilio_parser_get_int32_array(
    GRilIoParser* parser,
    gint32* values,
    guint count)
{
    return grilio_parser_get_uint32_array(parser, (guint32*)values, count);
}

gboolean
grilio_parser_get_uint32(
    GRilIoParser* parser,
    guint32* value)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    if ((p->ptr + 4) <= p->end) {
        if (value) {
            gint32* ptr = (gint32*)p->ptr;
            *value = GUINT32_FROM_RIL(*ptr);
        }
        p->ptr += 4;
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
grilio_parser_get_uint32_array(
    GRilIoParser* parser,
    guint32* values,
    guint count)
{
    guint i;
    for (i=0; i<count; i++) {
        if (values) {
            if (!grilio_parser_get_uint32(parser, values + i)) {
                return FALSE;
            }
        } else if (!grilio_parser_get_uint32(parser, NULL)) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
grilio_parser_get_nullable_utf8(
    GRilIoParser* parser,
    char** out)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    if ((p->ptr + 4) <= p->end) {
        const gint32* len_ptr = (gint32*)p->ptr;
        const gint32 len = GUINT32_FROM_RIL(*len_ptr);
        if (len == -1) {
            /* NULL string */
            p->ptr += 4;
            if (out) {
                *out = NULL;
            }
            return TRUE;
        } else if (len >= 0) {
            const guint32 padded_len = G_ALIGN4((len+1)*2);
            if ((p->ptr + padded_len + 4) <= p->end) {
                const gunichar2* utf16 = (const gunichar2*)(p->ptr + 4);
                p->ptr += padded_len + 4;
                if (out) {
                    *out = g_utf16_to_utf8(utf16, len, NULL, NULL, NULL);
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

char*
grilio_parser_get_utf8(
    GRilIoParser* parser)
{
    char* str = NULL;
    grilio_parser_get_nullable_utf8(parser, &str);
    return str;
}

char**
grilio_parser_split_utf8(
    GRilIoParser* parser,
    const gchar* delimiter)
{
    char** result = NULL;
    char* str = grilio_parser_get_utf8(parser);
    if (str) {
        result = g_strsplit(str, delimiter, -1);
        g_free(str);
    }
    return result;
}

gboolean
grilio_parser_skip_string(
    GRilIoParser* parser)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    if ((p->ptr + 4) <= p->end) {
        const gint32* len_ptr = (gint32*)p->ptr;
        const gint32 len = GUINT32_FROM_RIL(*len_ptr);
        if (len == -1) {
            /* NULL string */
            p->ptr += 4;
            return TRUE;
        } else if (len >= 0) {
            const guint32 padded_len = G_ALIGN4((len+1)*2);
            if ((p->ptr + padded_len + 4) <= p->end) {
                p->ptr += padded_len + 4;
                return TRUE;
            }
        }
    }
    return FALSE;
}

gsize
grilio_parser_bytes_remaining(
    GRilIoParser* parser)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    return p->end - p->ptr;
}

gsize
grilio_parser_get_data(
    GRilIoParser* parser,
    GRilIoParser* data,
    gsize maxlen)
{
    GRilIoParserPriv* p = grilio_parser_cast(parser);
    GRilIoParserPriv* pdata = grilio_parser_cast(data);
    const gsize maxlen2 = p->end - p->ptr;
    const gsize len = MIN(maxlen, maxlen2);
    pdata->ptr = p->ptr;
    pdata->end = pdata->ptr + len;
    p->ptr += len;
    return len;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
