/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#ifndef GRILIO_TRANSPORT_H
#define GRILIO_TRANSPORT_H

#include "grilio_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct grilio_transport_priv GRilIoTransportPriv;

struct grilio_transport {
    GObject parent;
    GRilIoTransportPriv* priv;
    gboolean connected;
    guint ril_version;
    const char* name;
    const char* log_prefix;
};

typedef enum grilio_send_status {
    GRILIO_SEND_OK,
    GRILIO_SEND_ERROR,
    GRILIO_SEND_PENDING
} GRILIO_SEND_STATUS;

typedef enum grilio_response_type {
    GRILIO_RESPONSE_NONE = -1,
    GRILIO_RESPONSE_SOLICITED,
    GRILIO_RESPONSE_SOLICITED_ACK,
    GRILIO_RESPONSE_SOLICITED_ACK_EXP,
} GRILIO_RESPONSE_TYPE;

typedef enum grilio_transport_indication_type {
    GRILIO_INDICATION_NONE = -1,
    GRILIO_INDICATION_UNSOLICITED,
    GRILIO_INDICATION_UNSOLICITED_ACK_EXP,
} GRILIO_INDICATION_TYPE;

GRilIoTransport*
grilio_transport_socket_new(
    int fd,
    const char* sub,
    gboolean can_close);

GRilIoTransport*
grilio_transport_socket_new_path(
    const char* path,
    const char* subscription);

GRilIoTransport*
grilio_transport_ref(
    GRilIoTransport* transport);

void
grilio_transport_unref(
    GRilIoTransport* transport);

void
grilio_transport_shutdown(
    GRilIoTransport* transport,
    gboolean flush);

G_END_DECLS

#endif /* GRILIO_TRANSPORT_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
