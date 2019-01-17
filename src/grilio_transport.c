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

#include "grilio_transport_impl.h"
#include "grilio_transport_p.h"
#include "grilio_log.h"

#include <gutil_misc.h>

struct grilio_transport_priv {
    char* name;
    char* log_prefix;
};

G_DEFINE_ABSTRACT_TYPE(GRilIoTransport, grilio_transport, G_TYPE_OBJECT)

#define PARENT_CLASS grilio_transport_parent_class
#define GRILIO_TRANSPORT(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransport)
#define GRILIO_TRANSPORT_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransportClass)
#define GRILIO_TRANSPORT_GET_CLASS(obj) \
    G_TYPE_INSTANCE_GET_CLASS((obj), GRILIO_TYPE_TRANSPORT, \
    GRilIoTransportClass)
#define GRILIO_IS_TRANSPORT_TYPE(klass) \
    G_TYPE_CHECK_CLASS_TYPE(klass, GRILIO_TYPE_TRANSPORT)

enum grilio_transport_signal {
    SIGNAL_CONNECTED,
    SIGNAL_DISCONNECTED,
    SIGNAL_REQUEST_SENT,
    SIGNAL_RESPONSE,
    SIGNAL_INDICATION,
    SIGNAL_READ_ERROR,
    SIGNAL_WRITE_ERROR,
    SIGNAL_COUNT
};

#define SIGNAL_CONNECTED_NAME       "grilio-transport-connected"
#define SIGNAL_DISCONNECTED_NAME    "grilio-transport-disconnected"
#define SIGNAL_REQUEST_SENT_NAME    "grilio-transport-request-sent"
#define SIGNAL_RESPONSE_NAME        "grilio-transport-response"
#define SIGNAL_INDICATION_NAME      "grilio-transport-indication"
#define SIGNAL_READ_ERROR_NAME      "grilio-transport-read-error"
#define SIGNAL_WRITE_ERROR_NAME     "grilio-transport-write-error"

static guint grilio_transport_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Internal API
 *==========================================================================*/

void
grilio_transport_signal_connected(
    GRilIoTransport* self)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_CONNECTED], 0);
}

void
grilio_transport_signal_disconnected(
    GRilIoTransport* self)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_DISCONNECTED], 0);
}

void
grilio_transport_signal_request_sent(
    GRilIoTransport* self,
    GRilIoRequest* req)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_REQUEST_SENT], 0, req);
}

void
grilio_transport_signal_response(
    GRilIoTransport* self,
    GRILIO_RESPONSE_TYPE type,
    guint serial,
    int status,
    const void* data,
    guint len)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_RESPONSE], 0,
        type, serial, status, data, len);
}

void
grilio_transport_signal_indication(
    GRilIoTransport* self,
    GRILIO_INDICATION_TYPE type,
    guint code,
    const void* data,
    guint len)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_INDICATION], 0,
        type, code, data, len);
}

void
grilio_transport_signal_read_error(
    GRilIoTransport* self,
    const GError* error)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_READ_ERROR], 0, error);
}

void
grilio_transport_signal_write_error(
    GRilIoTransport* self,
    const GError* error)
{
    g_signal_emit(self, grilio_transport_signals[SIGNAL_WRITE_ERROR], 0, error);
}

/*==========================================================================*
 * API
 *==========================================================================*/

GRilIoTransport*
grilio_transport_ref(
    GRilIoTransport* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GRILIO_TRANSPORT(self));
        return self;
    } else {
        return NULL;
    }
}

void
grilio_transport_unref(
    GRilIoTransport* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GRILIO_TRANSPORT(self));
    }
}

guint
grilio_transport_version_offset(
    GRilIoTransport* self)
{
    if (G_LIKELY(self)) {
        GRilIoTransportClass* klass = GRILIO_TRANSPORT_GET_CLASS(self);

        return klass->ril_version_offset;
    }
    return 0;
}

void
grilio_transport_set_name(
    GRilIoTransport* self,
    const char* name)
{
    if (G_LIKELY(self)) {
        GRilIoTransportPriv* priv = self->priv;

        g_free(priv->name);
        g_free(priv->log_prefix);
        self->name = priv->name = g_strdup(name);
        if (name && name[0]) {
            self->log_prefix = priv->log_prefix = g_strconcat(name, " ", NULL);
        } else {
            priv->log_prefix = NULL;
            self->log_prefix = "";
        }
    }
}

GRILIO_SEND_STATUS
grilio_transport_send(
    GRilIoTransport* self,
    GRilIoRequest* req,
    guint code)
{
    if (G_LIKELY(self) && G_LIKELY(req)) {
        GRilIoTransportClass* klass = GRILIO_TRANSPORT_GET_CLASS(self);

        return klass->send(self, req, code);
    }
    return GRILIO_SEND_ERROR;
}

void
grilio_transport_shutdown(
    GRilIoTransport* self,
    gboolean flush)
{
    if (G_LIKELY(self)) {
        GRilIoTransportClass* klass = GRILIO_TRANSPORT_GET_CLASS(self);

        klass->shutdown(self, flush);
    }
}

gulong
grilio_transport_add_connected_handler(
    GRilIoTransport* self,
    GRilIoTransportFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_CONNECTED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_disconnected_handler(
    GRilIoTransport* self,
    GRilIoTransportFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_DISCONNECTED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_request_sent_handler(
    GRilIoTransport* self,
    GRilIoTransportRequestFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_REQUEST_SENT_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_response_handler(
    GRilIoTransport* self,
    GRilIoTransportResponseFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_RESPONSE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_indication_handler(
    GRilIoTransport* self,
    GRilIoTransportIndicationFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_INDICATION_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_read_error_handler(
    GRilIoTransport* self,
    GRilIoTransportErrorFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_READ_ERROR_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
grilio_transport_add_write_error_handler(
    GRilIoTransport* self,
    GRilIoTransportErrorFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_WRITE_ERROR_NAME, G_CALLBACK(func), user_data) : 0;
}

void
grilio_transport_remove_handler(
    GRilIoTransport* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
grilio_transport_remove_handlers(
    GRilIoTransport* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
grilio_transport_init(
    GRilIoTransport* self)
{
    GRilIoTransportPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GRILIO_TYPE_TRANSPORT, GRilIoTransportPriv);

    self->priv = priv;
    self->name = "RIL";
    self->log_prefix = "RIL ";
}

static
void
grilio_transport_finalize(
    GObject* object)
{
    GRilIoTransport* self = GRILIO_TRANSPORT(object);
    GRilIoTransportPriv* priv = self->priv;

    g_free(priv->name);
    g_free(priv->log_prefix);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
grilio_transport_class_init(
    GRilIoTransportClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = grilio_transport_finalize;
    g_type_class_add_private(klass, sizeof(GRilIoTransportPriv));
    grilio_transport_signals[SIGNAL_CONNECTED] =
        g_signal_new(SIGNAL_CONNECTED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    grilio_transport_signals[SIGNAL_DISCONNECTED] =
        g_signal_new(SIGNAL_DISCONNECTED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    grilio_transport_signals[SIGNAL_REQUEST_SENT] =
        g_signal_new(SIGNAL_REQUEST_SENT_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_POINTER);
    grilio_transport_signals[SIGNAL_RESPONSE] =
        g_signal_new(SIGNAL_RESPONSE_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 5,
            G_TYPE_INT, G_TYPE_UINT, G_TYPE_INT, G_TYPE_POINTER, G_TYPE_UINT);
    grilio_transport_signals[SIGNAL_INDICATION] =
        g_signal_new(SIGNAL_INDICATION_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
            G_TYPE_INT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);
    grilio_transport_signals[SIGNAL_READ_ERROR] =
        g_signal_new(SIGNAL_READ_ERROR_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_ERROR);
    grilio_transport_signals[SIGNAL_WRITE_ERROR] =
        g_signal_new(SIGNAL_WRITE_ERROR_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
            G_TYPE_ERROR);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
