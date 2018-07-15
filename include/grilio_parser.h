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

#ifndef GRILIO_PARSER_H
#define GRILIO_PARSER_H

#include "grilio_types.h"

G_BEGIN_DECLS

struct grilio_parser {
    const void* d[4];
};

void
grilio_parser_init(
    GRilIoParser* parser,
    const void* data,
    gsize len);

gboolean
grilio_parser_at_end(
    GRilIoParser* parser);

gboolean
grilio_parser_get_byte(
    GRilIoParser* parser,
    guchar* value);

gboolean
grilio_parser_get_int32(
    GRilIoParser* parser,
    gint32* value);

gboolean
grilio_parser_get_int32_array(
    GRilIoParser* parser,
    gint32* values,
    guint count);

gboolean
grilio_parser_get_uint32(
    GRilIoParser* parser,
    guint32* value);

gboolean
grilio_parser_get_uint32_array(
    GRilIoParser* parser,
    guint32* values,
    guint count);

gboolean
grilio_parser_get_nullable_utf8(
    GRilIoParser* parser,
    char** str);

char*
grilio_parser_get_utf8(
    GRilIoParser* parser)
    G_GNUC_WARN_UNUSED_RESULT;

char**
grilio_parser_split_utf8(
    GRilIoParser* parser,
    const gchar* delimiter)
    G_GNUC_WARN_UNUSED_RESULT;

gboolean
grilio_parser_skip_string(
    GRilIoParser* parser);

gsize
grilio_parser_bytes_remaining(
    GRilIoParser* parser);

gsize
grilio_parser_get_data(
    GRilIoParser* parser,
    GRilIoParser* data,
    gsize maxlen);

G_END_DECLS

#endif /* GRILIO_PARSER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
