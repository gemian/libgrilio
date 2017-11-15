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

#ifndef GRILIO_TYPES_H
#define GRILIO_TYPES_H

#include <gutil_types.h>

G_BEGIN_DECLS

#define GRILIO_LOG_MODULE grilio_log
#define GRILIO_HEXDUMP_LOG_MODULE grilio_hexdump_log
extern GLogModule GRILIO_LOG_MODULE;
extern GLogModule GRILIO_HEXDUMP_LOG_MODULE;

typedef struct grilio_channel GRilIoChannel;
typedef struct grilio_parser GRilIoParser;
typedef struct grilio_request GRilIoRequest;
typedef struct grilio_queue GRilIoQueue;

typedef enum grilio_transaction_state {
    GRILIO_TRANSACTION_NONE,
    GRILIO_TRANSACTION_QUEUED,
    GRILIO_TRANSACTION_STARTED
} GRILIO_TRANSACTION_STATE;

typedef enum grilio_packet_type {
    GRILIO_PACKET_REQ,
    GRILIO_PACKET_RESP,
    GRILIO_PACKET_UNSOL,
    GRILIO_PACKET_ACK,
    GRILIO_PACKET_RESP_ACK_EXP,
    GRILIO_PACKET_UNSOL_ACK_EXP
} GRILIO_PACKET_TYPE;

#define GRILIO_TIMEOUT_NONE     (0)     /* Infinite timeout */
#define GRILIO_TIMEOUT_DEFAULT  (-1)

G_END_DECLS

#endif /* GRILIO_TYPES_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
