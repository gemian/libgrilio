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

#ifndef GRILIO_TEST_SERVER_H
#define GRILIO_TEST_SERVER_H

#include "grilio_types.h"

typedef struct grilio_test_server GRilIoTestServer;

#define GRILIO_RIL_VERSION 7

typedef
void
(*GRilIoTestRequestFunc)(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data);

GRilIoTestServer*
grilio_test_server_new(
    gboolean expect_sub);

void
grilio_test_server_free(
    GRilIoTestServer* server);

int
grilio_test_server_fd(
    GRilIoTestServer* server);

void
grilio_test_server_set_chunk(
    GRilIoTestServer* server,
    int chunk);

void
grilio_test_server_shutdown(
    GRilIoTestServer* server);

void
grilio_test_server_add_data(
    GRilIoTestServer* server,
    const void* data,
    guint len);

void
grilio_test_server_add_ack(
    GRilIoTestServer* server,
    guint id);

void
grilio_test_server_add_response(
    GRilIoTestServer* server,
    GRilIoRequest* req,
    guint id,
    guint status);

void
grilio_test_server_add_response_data(
    GRilIoTestServer* server,
    guint id,
    guint status,
    const void* data,
    guint len);

void
grilio_test_server_add_response_ack_exp(
    GRilIoTestServer* server,
    GRilIoRequest* req,
    guint id,
    guint status);

void
grilio_test_server_add_response_ack_exp_data(
    GRilIoTestServer* server,
    guint id,
    guint status,
    const void* data,
    guint len);

void
grilio_test_server_add_unsol_data(
    GRilIoTestServer* server,
    guint code,
    const void* data,
    guint len);

void
grilio_test_server_add_unsol(
    GRilIoTestServer* server,
    GRilIoRequest* req,
    guint code);

void
grilio_test_server_add_unsol_ack_exp_data(
    GRilIoTestServer* server,
    guint code,
    const void* data,
    guint len);

void
grilio_test_server_add_unsol_ack_exp(
    GRilIoTestServer* server,
    GRilIoRequest* req,
    guint code);

void
grilio_test_server_add_request_func(
    GRilIoTestServer* server,
    guint code,
    GRilIoTestRequestFunc fn,
    void* user_data);

#endif /* GRILIO_TEST_SERVER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
