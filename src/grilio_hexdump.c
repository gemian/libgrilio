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

#include "grilio_channel.h"

#include <ctype.h>

#define GLOG_MODULE_NAME GRILIO_HEXDUMP_LOG_MODULE
#include <gutil_log.h>

/* Sub-module to turn prefix off */
GLogModule GLOG_MODULE_NAME = {
    NULL,                   /* name */
    &GRILIO_LOG_MODULE,     /* parent */
    NULL,                   /* reserved */
    GLOG_LEVEL_MAX,         /* max_level */
    GLOG_LEVEL_INHERIT,     /* level */
    GLOG_FLAG_HIDE_NAME     /* flags */
};

guint
grilio_log_hexdump_line(
    char* buf,
    const void* data,
    guint len)
{
    static const char hex[] = "0123456789abcdef";
    const guint bytes_to_print = MIN(len, 16);
    const guchar* bytes = data;
    char* ptr = buf;
    guint i;

    for (i=0; i<16; i++) {
        if (i > 0) {
            *ptr++ = ' ';
            if (i == 8) *ptr++ = ' ';
        }
        if (i < len) {
            const guchar b = bytes[i];
            *ptr++ = hex[(b >> 4) & 0xf];
            *ptr++ = hex[b & 0xf];
        } else {
            *ptr++ = ' ';
            *ptr++ = ' ';
        }
    }

    *ptr++ = ' ';
    *ptr++ = ' ';
    *ptr++ = ' ';
    *ptr++ = ' ';
    for (i=0; i<bytes_to_print; i++) {
        const char c = bytes[i];
        if (i == 8) *ptr++ = ' ';
        *ptr++ = isprint(c) ? c : '.';
    }

    *ptr++ = 0;
    return ptr - buf;
}

static
void
grilio_channel_log_default(
    GRilIoChannel* channel,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    guint data_len,
    void* user_data)
{
    const int level = GPOINTER_TO_INT(user_data);
    const GLogModule* module = &GLOG_MODULE_NAME;
    if (gutil_log_enabled(module, level)) {
        const char* prefix = channel->name ? channel->name : "";
        char dir = (type == GRILIO_PACKET_REQ) ? '<' : '>';
        char buf[80];
        guint off = 0;
        while (off < data_len) {
            const guint len = MIN(data_len - off, 16);
            grilio_log_hexdump_line(buf, ((const guchar*)data) + off, len);
            gutil_log(module, level, "%s%c %04x: %s", prefix, dir, off, buf);
            off += len;
            dir = ' ';
        }
    }
}

guint
grilio_channel_add_default_logger(
    GRilIoChannel* channel,
    int level)
{
    return grilio_channel_add_logger(channel, grilio_channel_log_default,
        GINT_TO_POINTER(level));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
