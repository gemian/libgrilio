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

#ifndef GRILIO_TRANSPORT_PRIVATE_H
#define GRILIO_TRANSPORT_PRIVATE_H

#include "grilio_transport.h"

typedef
void
(*GRilIoTransportFunc)(
    GRilIoTransport* transport,
    void* user_data);

typedef
void
(*GRilIoTransportErrorFunc)(
    GRilIoTransport* transport,
    const GError* error,
    void* user_data);

typedef
void
(*GRilIoTransportRequestFunc)(
    GRilIoTransport* transport,
    GRilIoRequest* req,
    void* user_data);

typedef
void
(*GRilIoTransportResponseFunc)(
    GRilIoTransport* transport,
    GRILIO_RESPONSE_TYPE type,
    guint serial,
    int status,
    const void* data,
    guint len,
    void* user_data);

typedef
void
(*GRilIoTransportIndicationFunc)(
    GRilIoTransport* transport,
    GRILIO_INDICATION_TYPE type,
    guint code,
    const void* data,
    guint len,
    void* user_data);

guint
grilio_transport_version_offset(
    GRilIoTransport* transport);

void
grilio_transport_set_name(
    GRilIoTransport* transport,
    const char* name);

void
grilio_transport_set_channel(
    GRilIoTransport* transport,
    GRilIoChannel* channel);

GRILIO_SEND_STATUS
grilio_transport_send(
    GRilIoTransport* transport,
    GRilIoRequest* req,
    guint code);

gulong
grilio_transport_add_connected_handler(
    GRilIoTransport* transport,
    GRilIoTransportFunc func,
    void* user_data);

gulong
grilio_transport_add_disconnected_handler(
    GRilIoTransport* transport,
    GRilIoTransportFunc func,
    void* user_data);

gulong
grilio_transport_add_request_sent_handler(
    GRilIoTransport* transport,
    GRilIoTransportRequestFunc func,
    void* user_data);

gulong
grilio_transport_add_response_handler(
    GRilIoTransport* transport,
    GRilIoTransportResponseFunc func,
    void* user_data);

gulong
grilio_transport_add_indication_handler(
    GRilIoTransport* transport,
    GRilIoTransportIndicationFunc func,
    void* user_data);

gulong
grilio_transport_add_read_error_handler(
    GRilIoTransport* transport,
    GRilIoTransportErrorFunc func,
    void* user_data);

gulong
grilio_transport_add_write_error_handler(
    GRilIoTransport* transport,
    GRilIoTransportErrorFunc func,
    void* user_data);

void
grilio_transport_remove_handler(
    GRilIoTransport* transport,
    gulong id);

void
grilio_transport_remove_handlers(
    GRilIoTransport* transport,
    gulong* ids,
    guint count);

#define grilio_transport_remove_all_handlers(transport,ids) \
    grilio_transport_remove_handlers(transport, ids, G_N_ELEMENTS(ids))

#endif /* GRILIO_TRANSPORT_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
