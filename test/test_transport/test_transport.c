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

#include "test_common.h"

#include "grilio_transport_p.h"
#include "grilio_transport_impl.h"

#include "grilio_test_server.h"

static TestOpt test_opt;

static
void
test_dummy_cb(
    GRilIoTransport* transport,
    void* user_data)
{
}

/*==========================================================================*
 * Basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    gulong id;
    GRilIoTestServer* server = grilio_test_server_new(TRUE);
    GRilIoTransport* trans = grilio_transport_socket_new
        (grilio_test_server_fd(server), NULL, FALSE);

    /* NULL tolerance */
    g_assert(!grilio_transport_socket_new(-1, NULL, TRUE));
    g_assert(!grilio_transport_ref(NULL));
    grilio_transport_unref(NULL);
    g_assert(grilio_transport_version_offset(NULL) == 0);
    grilio_transport_set_name(NULL, NULL);
    grilio_transport_set_id_gen(NULL, NULL);
    g_assert(!grilio_transport_get_id(NULL));
    grilio_transport_release_id(NULL, 0);
    g_assert(grilio_transport_send(NULL, NULL, 0) == GRILIO_SEND_ERROR);
    g_assert(grilio_transport_send(trans, NULL, 0) == GRILIO_SEND_ERROR);
    grilio_transport_shutdown(NULL, FALSE);
    g_assert(!grilio_transport_add_connected_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_connected_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_disconnected_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_disconnected_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_request_sent_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_request_sent_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_response_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_response_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_indication_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_indication_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_read_error_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_read_error_handler(trans, NULL, NULL));
    g_assert(!grilio_transport_add_write_error_handler(NULL, NULL, NULL));
    g_assert(!grilio_transport_add_write_error_handler(trans, NULL, NULL));
    grilio_transport_remove_handler(NULL, 0);
    grilio_transport_remove_handler(trans, 0);

    id = grilio_transport_add_connected_handler(trans, test_dummy_cb, NULL);
    g_assert(id);
    grilio_transport_remove_handler(trans, id);

    /* No id generator - no id */
    g_assert(!grilio_transport_get_id(trans));
    grilio_transport_release_id(trans, 0);

    g_assert(grilio_transport_ref(trans) == trans);
    grilio_transport_unref(trans);
    grilio_transport_unref(trans);
    grilio_test_server_free(server);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/transport/"

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "Basic", test_basic);
    signal(SIGPIPE, SIG_IGN);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
