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

#ifndef GRILIO_TRANSPORT_IMPL_H
#define GRILIO_TRANSPORT_IMPL_H

#include "grilio_transport.h"

/* Internal API for use by GRilIoTransport implemenations */

G_BEGIN_DECLS

typedef struct grilio_transport_class {
    GObjectClass parent;
    guint ril_version_offset;

    GRILIO_SEND_STATUS (*send)(GRilIoTransport* transport,
        GRilIoRequest* req, guint code);
    void (*shutdown)(GRilIoTransport* transport, gboolean flush);

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
} GRilIoTransportClass;

GType grilio_transport_get_type(void);
#define GRILIO_TYPE_TRANSPORT (grilio_transport_get_type())
#define GRILIO_TRANSPORT(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransport)
#define GRILIO_TRANSPORT_GET_CLASS(obj) \
    G_TYPE_INSTANCE_GET_CLASS((obj), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransportClass)
#define GRILIO_TRANSPORT_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransportClass)

guint
grilio_transport_get_id(
    GRilIoTransport* transport); /* Since 1.0.28 */

void
grilio_transport_release_id(
    GRilIoTransport* transport,
    guint id); /* Since 1.0.28 */

void
grilio_transport_signal_connected(
    GRilIoTransport* transport);

void
grilio_transport_signal_disconnected(
    GRilIoTransport* transport);

void
grilio_transport_signal_request_sent(
    GRilIoTransport* transport,
    GRilIoRequest* req);

void
grilio_transport_signal_response(
    GRilIoTransport* transport,
    GRILIO_RESPONSE_TYPE type,
    guint serial,
    int status,
    const void* data,
    guint len);

void
grilio_transport_signal_indication(
    GRilIoTransport* transport,
    GRILIO_INDICATION_TYPE type,
    guint code,
    const void* data,
    guint len);

void
grilio_transport_signal_read_error(
    GRilIoTransport* transport,
    const GError* error);

void
grilio_transport_signal_write_error(
    GRilIoTransport* transport,
    const GError* error);

G_END_DECLS

#endif /* GRILIO_TRANSPORT_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
