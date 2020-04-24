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

#include "grilio_p.h"

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

static
void
grilio_log_hexdump_line(
    char* buf,
    const void* data1,
    guint len1,
    const void* data2,
    guint len2)
{
    static const char hex[] = "0123456789abcdef";
    const guchar* bytes1 = data1;
    const guchar* bytes2 = data2;
    guint total = 0;
    char* ptr = buf;
    guint i;

    for (i = 0; i < 16; i++) {
        if (i > 0) {
            *ptr++ = ' ';
            if (i == 8) *ptr++ = ' ';
        }
        if (i < len1) {
            const guchar b = bytes1[i];
            *ptr++ = hex[(b >> 4) & 0xf];
            *ptr++ = hex[b & 0xf];
            total++;
        } else {
            const int j = i - len1;
            if (j < len2) {
                const guchar b = bytes2[j];
                *ptr++ = hex[(b >> 4) & 0xf];
                *ptr++ = hex[b & 0xf];
                total++;
            } else {
                *ptr++ = ' ';
                *ptr++ = ' ';
            }
        }
    }

    *ptr++ = ' ';
    *ptr++ = ' ';
    *ptr++ = ' ';
    *ptr++ = ' ';
    for (i = 0; i < total; i++) {
        const char c = (i < len1) ? bytes1[i] : bytes2[i - len1];
        if (i == 8) *ptr++ = ' ';
        *ptr++ = isprint(c) ? c : '.';
    }

    *ptr++ = 0;
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
        const guchar* bytes = data;
        char dir = (type == GRILIO_PACKET_REQ) ? '<' : '>';
        char buf[80];
        guint off = 0;
        guint8 header_buf[RIL_MAX_HEADER_SIZE];
        guint32* header = (guint32*)header_buf;
        guint header_len;
        guint ril_code;

        /* Fake RIL socket header (for historical reasons) */
        switch (type) {
        case GRILIO_PACKET_REQ:
            header_len = RIL_REQUEST_HEADER_SIZE;
            ril_code = code;
            break;
        default:
        case GRILIO_PACKET_RESP:
            header_len = RIL_RESPONSE_HEADER_SIZE;
            ril_code = RIL_PACKET_TYPE_SOLICITED;
            break;
        case GRILIO_PACKET_RESP_ACK_EXP:
            header_len = RIL_RESPONSE_HEADER_SIZE;
            ril_code = RIL_PACKET_TYPE_SOLICITED_ACK_EXP;
            break;
        case GRILIO_PACKET_UNSOL:
            header_len = RIL_UNSOL_HEADER_SIZE;
            ril_code = RIL_PACKET_TYPE_UNSOLICITED;
            break;
        case GRILIO_PACKET_UNSOL_ACK_EXP:
            header_len = RIL_UNSOL_HEADER_SIZE;
            ril_code = RIL_PACKET_TYPE_UNSOLICITED_ACK_EXP;
            break;
        case GRILIO_PACKET_ACK:
            header_len = RIL_ACK_HEADER_SIZE;
            ril_code = RIL_PACKET_TYPE_SOLICITED_ACK;
            break;
        }

        header[0] = GUINT32_TO_RIL(ril_code);
        switch (type) {
        default:
        case GRILIO_PACKET_RESP:
        case GRILIO_PACKET_RESP_ACK_EXP:
            header[2] = GUINT32_TO_RIL(code); /* status */
            /* no break */
        case GRILIO_PACKET_REQ:
        case GRILIO_PACKET_ACK:
            header[1] = GUINT32_TO_RIL(id);
            break;
        case GRILIO_PACKET_UNSOL_ACK_EXP:
        case GRILIO_PACKET_UNSOL:
            header[1] = GUINT32_TO_RIL(code);
            break;
        }

        while (header_len > 0 || off < data_len) {
            const guint maxlen = 16 - header_len;
            const guint len = MIN(data_len - off, maxlen);
            grilio_log_hexdump_line(buf, header, header_len, bytes + off, len);
            gutil_log(module, level, "%s%c %04x: %s", prefix, dir, off, buf);
            header_len = 0;
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
    return grilio_channel_add_logger2(channel, grilio_channel_log_default,
        GINT_TO_POINTER(level));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
