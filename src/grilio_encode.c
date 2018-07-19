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

#include "grilio_encode.h"

#include <gutil_macros.h>

GByteArray*
grilio_encode_byte(
    GByteArray* dest,
    guchar value)
{
    if (!dest) {
        dest = g_byte_array_new();
    }
    g_byte_array_set_size(dest, dest->len + 1);
    dest->data[dest->len - 1] = value;
    return dest;
}

GByteArray*
grilio_encode_bytes(
    GByteArray* dest,
    const void* data,
    guint len)
{
    if (len > 0) {
        gsize old_size;
        if (!dest) {
            dest = g_byte_array_sized_new(len);
        }
        old_size = dest->len;
        g_byte_array_set_size(dest, old_size + len);
        memcpy(dest->data + old_size, data, len);
    }
    return dest;
}

GByteArray*
grilio_encode_int32(
    GByteArray* dest,
    guint32 value)
{
    gsize old_size;
    if (!dest) {
        dest = g_byte_array_sized_new(sizeof(value));
    }
    old_size = dest->len;
    g_byte_array_set_size(dest, old_size + sizeof(value));
    ((guint32*)(dest->data + old_size))[0] = value;
    return dest;
}

GByteArray*
grilio_encode_int32_values(
    GByteArray* dest,
    const gint32* values,
    guint count)
{
    return grilio_encode_uint32_values(dest, (const guint32*)values, count);
}

GByteArray*
grilio_encode_uint32_values(
    GByteArray* dest,
    const guint32* values,
    guint count)
{
    if (count > 0) {
        gsize i, old_size;
        guint32* ptr;

        if (!dest) {
            dest = g_byte_array_sized_new(count * sizeof(values[0]));
        }

        old_size = dest->len;
        g_byte_array_set_size(dest, old_size + count * sizeof(values[0]));
        ptr = (guint32*)(dest->data + old_size);
        for (i = 0; i < count; i++) {
            ptr[i] = values[i];
        }
    }
    return dest;
}

GByteArray*
grilio_encode_utf8(
    GByteArray* dest,
    const char* utf8)
{
    const gssize num_bytes = utf8 ? strlen(utf8) : 0;

    return grilio_encode_utf8_chars(dest, utf8, num_bytes);
}

GByteArray*
grilio_encode_utf8_chars(
    GByteArray* dest,
    const char* utf8,
    gssize num_bytes)
{
    gsize old_size = dest ? dest->len : 0;

    if (utf8) {
        const char* end = utf8;
        g_utf8_validate(utf8, num_bytes, &end);
        num_bytes = end - utf8;
    } else {
        num_bytes = 0;
    }

    if (num_bytes > 0) {
        glong len = g_utf8_strlen(utf8, num_bytes);
        gsize padded_len = G_ALIGN4((len + 1) * 2);
        glong utf16_len = 0;
        guint32* len_ptr;
        gunichar2* utf16_ptr;
        gunichar2* utf16;

        /* Preallocate space */
        if (!dest) {
            dest = g_byte_array_sized_new(padded_len + 4);
        }

        g_byte_array_set_size(dest, old_size + padded_len + 4);
        len_ptr = (guint32*)(dest->data + old_size);
        utf16_ptr = (gunichar2*)(len_ptr + 1);

        utf16 = g_utf8_to_utf16(utf8, num_bytes, NULL, &utf16_len, NULL);
        len = utf16_len;
        padded_len = G_ALIGN4((len + 1) * 2);
        if (utf16) {
            memcpy(utf16_ptr, utf16, (len + 1 ) * 2);
            g_free(utf16);
        }

        /* Actual length */
        *len_ptr = len;

        /* Zero padding */
        if (padded_len - (len + 1) * 2) {
            memset(utf16_ptr + (len + 1), 0, padded_len - (len + 1)*2);
        }

        /* Correct the packet size if necessaary */
        g_byte_array_set_size(dest, old_size + padded_len + 4);
        return dest;
    } else if (utf8) {
        /* Empty string */
        guint16* ptr16;

        if (!dest) {
            dest = g_byte_array_sized_new(8);
        }
        g_byte_array_set_size(dest, old_size + 8);
        ptr16 = (guint16*)(dest->data + old_size);
        ptr16[0] = ptr16[1] = ptr16[2] = 0; ptr16[3] = 0xffff;
        return dest;
    } else {
        /* NULL string */
        return grilio_encode_int32(dest, -1);
    }
}

GByteArray*
grilio_encode_format(
    GByteArray* dest,
    const char* format,
    ...)
{
    va_list va;

    va_start(va, format);
    dest = grilio_encode_format_va(dest, format, va);
    va_end(va);
    return dest;
}

GByteArray*
grilio_encode_format_va(
    GByteArray* dest,
    const char* format,
    va_list va)
{
    char* text = g_strdup_vprintf(format, va);

    dest = grilio_encode_utf8(dest, text);
    g_free(text);
    return dest;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
