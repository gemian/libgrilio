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

#ifndef GRILIO_ENCODE_H
#define GRILIO_ENCODE_H

#include "grilio_types.h"

/* Introduced in version 1.0.25 */

G_BEGIN_DECLS

GByteArray*
grilio_encode_byte(
    GByteArray* dest,
    guchar value);

GByteArray*
grilio_encode_bytes(
    GByteArray* dest,
    const void* data,
    guint len);

GByteArray*
grilio_encode_int32(
    GByteArray* dest,
    guint32 value);

GByteArray*
grilio_encode_int32_values(
    GByteArray* dest,
    const gint32* values,
    guint count);

GByteArray*
grilio_encode_uint32_values(
    GByteArray* dest,
    const guint32* values,
    guint count);

GByteArray*
grilio_encode_utf8(
    GByteArray* dest,
    const char* utf8);

GByteArray*
grilio_encode_utf8_chars(
    GByteArray* dest,
    const char* utf8,
    gssize num_bytes);

GByteArray*
grilio_encode_format(
    GByteArray* dest,
    const char* format,
    ...) G_GNUC_PRINTF(2,3);

GByteArray*
grilio_encode_format_va(
    GByteArray* dest,
    const char* format,
    va_list va);

G_END_DECLS

#endif /* GRILIO_ENCODE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
