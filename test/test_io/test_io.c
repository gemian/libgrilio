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

#include "test_common.h"

#include "grilio_channel.h"
#include "grilio_request.h"
#include "grilio_parser.h"
#include "grilio_queue.h"

#include "grilio_test_server.h"
#include "grilio_p.h"

#include <gutil_log.h>
#include <gutil_macros.h>

#define TEST_TIMEOUT (10) /* seconds */

#define RIL_REQUEST_TEST_0 (10)
#define RIL_REQUEST_TEST_1 (11)
#define RIL_REQUEST_TEST_2 (12)
#define RIL_REQUEST_TEST_3 (13)
#define RIL_REQUEST_TEST_4 (14)
#define RIL_REQUEST_TEST RIL_REQUEST_TEST_0

#define RIL_E_GENERIC_FAILURE 2
#define RIL_E_REQUEST_NOT_SUPPORTED 6

static TestOpt test_opt;

typedef struct test_common_data {
    const char* name;
    GMainLoop* loop;
    GRilIoTestServer* server;
    GRilIoChannel* io;
    guint timeout_id;
    guint log;
} Test;

#define test_new(type,name) ((type *)test_alloc(name, sizeof(type)))

static
gboolean
test_timeout_expired(
    gpointer data)
{
    Test* test = data;
    test->timeout_id = 0;
    g_main_loop_quit(test->loop);
    GERR("%s TIMEOUT", test->name);
    return G_SOURCE_REMOVE;
}

static
void
test_response_empty_ok(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    grilio_test_server_add_response_data(test->server, id,
        GRILIO_STATUS_OK, NULL, 0);
}

static
void
test_response_reflect_ok(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    grilio_test_server_add_response_data(test->server, id, GRILIO_STATUS_OK,
        data, len);
}

static
void*
test_alloc(
    const char* name,
    gsize size)
{
    Test* test = g_malloc0(size);
    GRilIoTestServer* server = grilio_test_server_new();
    int fd = grilio_test_server_fd(server);
    memset(test, 0, sizeof(*test));
    test->name = name;
    test->loop = g_main_loop_new(NULL, FALSE);
    test->server = server;
    test->io = grilio_channel_new_fd(fd, "SUB1", FALSE);
    grilio_channel_set_name(test->io, "TEST");
    grilio_channel_set_name(NULL, NULL); /* This one does nothing */
    test->log = grilio_channel_add_default_logger(test->io, GLOG_LEVEL_VERBOSE);
    if (!(test_opt.flags & TEST_FLAG_DEBUG)) {
        test->timeout_id = g_timeout_add_seconds(TEST_TIMEOUT,
            test_timeout_expired, test);
    }
    return test;
}

static
void
test_free(
    Test* test)
{
    g_assert((test_opt.flags & TEST_FLAG_DEBUG) || test->timeout_id);
    if (test->timeout_id) g_source_remove(test->timeout_id);
    /* These function should handle NULL arguments */
    grilio_channel_remove_logger(NULL, 0);
    grilio_channel_shutdown(NULL, FALSE);
    grilio_channel_unref(NULL);
    /* Remove logger twice, the second call should do nothing */
    grilio_channel_remove_logger(test->io, test->log);
    grilio_channel_remove_logger(test->io, test->log);
    grilio_channel_shutdown(test->io, FALSE);
    grilio_channel_unref(test->io);
    g_main_loop_unref(test->loop);
    grilio_test_server_free(test->server);
    g_free(test);
}

/*==========================================================================*
 * Connected
 *==========================================================================*/

typedef struct test_connected_data {
    Test test;
    gulong event_id;
    gulong connected_id;
    int event_count;
} TestConnected;

static
void
test_connected_event(
    GRilIoChannel* io,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    TestConnected* test = user_data;
    GRilIoParser parser;
    int count = 0;
    guint32 version = 0;

    g_assert(code == RIL_UNSOL_RIL_CONNECTED);
    grilio_parser_init(&parser, data, len);
    g_assert(grilio_parser_get_int32(&parser, &count));
    g_assert(grilio_parser_get_uint32(&parser, &version));
    g_assert(grilio_parser_at_end(&parser));

    GDEBUG("RIL version %u", version);
    g_assert(count == 1);
    g_assert(version == GRILIO_RIL_VERSION);
    grilio_channel_remove_handler(test->test.io, test->event_id);
    test->event_id = 0;
    test->event_count++;

    if (test->event_count == 2) {
        g_main_loop_quit(test->test.loop);
    }
}

static
void
test_connected_callback(
    GRilIoChannel* io,
    void* user_data)
{
    TestConnected* test = user_data;
    grilio_channel_remove_handler(test->test.io, test->connected_id);
    test->connected_id = 0;
    test->event_count++;
    if (test->event_count == 2) {
        g_main_loop_quit(test->test.loop);
    }
}

static
void
test_connected(
    void)
{
    TestConnected* data = test_new(TestConnected, "Connected");
    Test* test = &data->test;

    data->event_id = grilio_channel_add_unsol_event_handler(test->io,
            test_connected_event, RIL_UNSOL_RIL_CONNECTED, data);
    g_assert(data->event_id);
    data->connected_id = grilio_channel_add_connected_handler(test->io,
            test_connected_callback, data);
    g_assert(data->connected_id);

    g_main_loop_run(test->loop);
    g_assert(data->event_count == 2);
    g_assert(!data->connected_id);
    g_assert(!data->event_id);

    test_free(test);
}

/*==========================================================================*
 * Basic
 *==========================================================================*/

#define BASIC_RESPONSE_TEST "TEST"

static
void
test_basic_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GRilIoParser parser;
    char* text;

    g_assert(status == GRILIO_STATUS_OK);

    /* Unpack the string */
    grilio_parser_init(&parser, data, len);
    text = grilio_parser_get_utf8(&parser);
    g_assert(grilio_parser_at_end(&parser));
    g_assert(text);
    GDEBUG("%s", text);
    g_assert(!g_strcmp0(text, BASIC_RESPONSE_TEST));
    g_free(text);

    /* Skip the string */
    grilio_parser_init(&parser, data, len);
    g_assert(grilio_parser_skip_string(&parser));
    g_assert(grilio_parser_at_end(&parser));

    /* Done */
    g_main_loop_quit(test->loop);
}

static
guint
test_basic_request(
    Test* test,
    GRilIoChannelResponseFunc response)
{
    return grilio_channel_send_request_full(test->io, NULL,
        RIL_REQUEST_TEST, response, NULL, test);
}

static
gboolean
test_basic_response_ok(
    GRilIoTestServer* server,
    const char* data,
    guint id)
{
    if (id) {
        GRilIoRequest* resp = grilio_request_new();
        grilio_request_append_utf8(resp, data);
        grilio_test_server_add_response(server, resp, id, 0);
        grilio_request_unref(resp);
        return TRUE;
    }
    return FALSE;
}

static
void
test_basic(
    void)
{
    Test* test = test_new(Test, "Basic");
    GRilIoRequest* req = grilio_request_new();
    guint id;

    /* Test NULL resistance */
    g_assert(!grilio_request_retry_count(NULL));
    grilio_request_set_retry(NULL, 0, 0);
    grilio_request_set_retry_func(NULL, NULL);
    grilio_channel_set_timeout(NULL, 0);
    grilio_request_set_blocking(NULL, FALSE);
    grilio_channel_cancel_all(NULL, FALSE);
    grilio_channel_deserialize(NULL, 0);
    g_assert(!grilio_channel_serialize(NULL));
    g_assert(!grilio_channel_ref(NULL));
    g_assert(!grilio_channel_add_connected_handler(NULL, NULL, NULL));
    g_assert(!grilio_channel_add_connected_handler(test->io, NULL, NULL));
    g_assert(!grilio_channel_add_disconnected_handler(NULL, NULL, NULL));
    g_assert(!grilio_channel_add_disconnected_handler(test->io, NULL, NULL));
    g_assert(!grilio_channel_add_unsol_event_handler(NULL, NULL, 0, NULL));
    g_assert(!grilio_channel_add_unsol_event_handler(test->io, NULL, 0, NULL));
    g_assert(!grilio_channel_send_request(NULL, NULL, 0));
    g_assert(!grilio_channel_get_request(NULL, 0));
    g_assert(!grilio_channel_get_request(test->io, 0));
    g_assert(!grilio_channel_get_request(test->io, INT_MAX));

    /* Test send/cancel before we are connected to the server. */
    id = grilio_channel_send_request(test->io, NULL, 0);
    g_assert(grilio_channel_cancel_request(test->io, id, FALSE));
    grilio_test_server_set_chunk(test->server, 5);

    /* Submit repeatable request without the completion callback */
    grilio_request_set_retry(req, 0, 1);
    grilio_request_set_retry_func(req, NULL);
    g_assert(test_basic_response_ok(test->server, "IGNORE",
        grilio_channel_send_request(test->io, req, RIL_REQUEST_TEST)));

    /* This one has a callback which will terminate the test */
    g_assert(test_basic_response_ok(test->server, BASIC_RESPONSE_TEST,
        test_basic_request(test, test_basic_response)));

    g_main_loop_run(test->loop);
    g_assert(grilio_request_status(req) == GRILIO_REQUEST_DONE);
    grilio_request_unref(req);
    test_free(test);
}

/*==========================================================================*
 * Queue
 *==========================================================================*/

typedef struct test_queue_data {
    Test test;
    int cancel_count;
    int success_count;
    int destroy_count;
    GRilIoQueue* queue[3];
    gulong connected_id;
    guint cancel_id;
    guint last_id;
} TestQueue;

static
void
test_queue_no_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_queue_destroy_request(
    void* user_data)
{
    TestQueue* t = user_data;
    GDEBUG("Request destroyed");
    t->destroy_count++;
}

static
void
test_queue_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestQueue* t = user_data;
    if (status == GRILIO_STATUS_CANCELLED) { 
        t->cancel_count++;
        GDEBUG("%d request(s) cancelled", t->cancel_count);
    } else if (status == GRILIO_STATUS_OK) {
        t->success_count++;
        GDEBUG("%d request(s) cancelled", t->cancel_count);
    } else {
        GERR("Unexpected response status %d", status);
    }
}

static
void
test_queue_last_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestQueue* t = user_data;
    GDEBUG("Last response status %d", status);
    g_assert(status == GRILIO_STATUS_OK);

    /* 3 events should be cancelled, first one succeed,
     * this one doesn't count */
    g_assert(t->cancel_count == 3);
    g_assert(t->success_count == 1);
    g_assert(t->destroy_count == 1);
    g_main_loop_quit(t->test.loop);
}

static
void
test_queue_first_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestQueue* t = user_data;
    GDEBUG("First response status %d", status);
    if (status == GRILIO_STATUS_OK) {
        t->success_count++;
        grilio_queue_cancel_all(t->queue[1], TRUE);
        grilio_queue_cancel_request(t->queue[0], t->cancel_id, TRUE);

        g_assert(!t->last_id);
        t->last_id = test_basic_request(&t->test, test_queue_last_response);

        /* This one stops the event loop */
        test_basic_response_ok(t->test.server, "TEST", t->last_id);

        /* This will deallocate the queue, cancelling all the requests in
         * the process. Callbacks won't be notified. Extra ref just improves
         * the code coverage. */
        grilio_queue_ref(t->queue[2]);
        grilio_queue_unref(t->queue[2]);
        grilio_queue_unref(t->queue[2]);
        t->queue[2] = NULL;
    }
}

static
void
test_queue_start(
    GRilIoChannel* channel,
    void* user_data)
{
    TestQueue* t = user_data;
    guint id;

    /* NULL resistance */
    g_assert(!grilio_queue_send_request_full(NULL, NULL, 0, NULL, NULL, NULL));

    /* This entire queue will be cancelled */
    grilio_queue_send_request_full(t->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);
    grilio_queue_send_request_full(t->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);

    /* Expected failure to cancel a request that's not in a queue */
    id = grilio_channel_send_request_full(t->test.io, NULL,
        RIL_REQUEST_TEST, test_queue_no_response, NULL, NULL);
    g_assert(id);
    g_assert(!grilio_queue_cancel_request(t->queue[0], id, FALSE));
    g_assert(grilio_channel_cancel_request(t->test.io, id, FALSE));

    /* Cancel request without callback */
    grilio_queue_cancel_request(t->queue[1],
        grilio_queue_send_request(t->queue[1], NULL,
            RIL_REQUEST_TEST), FALSE);

    /* This one will be cancelled implicitely, when queue will get
     * deallocated. Callbacks won't be notified. */
    grilio_queue_send_request_full(t->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);
    grilio_queue_send_request_full(t->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);
    grilio_queue_send_request_full(t->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);
    grilio_queue_send_request(t->queue[2], NULL,
        RIL_REQUEST_TEST);

    /* This one will succeed */
    test_basic_response_ok(t->test.server, "QUEUE_TEST",
        grilio_queue_send_request_full(t->queue[0], NULL,
            RIL_REQUEST_TEST, test_queue_first_response,
            test_queue_destroy_request, t));

    /* This one from queue 0 will be cancelled too */
    t->cancel_id = grilio_queue_send_request_full(t->queue[0], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, t);
    test_basic_response_ok(t->test.server, "CANCEL", t->cancel_id);
}

static
void
test_queue(
    void)
{
    TestQueue* t = test_new(TestQueue, "Queue");
    Test* test = &t->test;

    t->queue[0] = grilio_queue_new(test->io);
    t->queue[1] = grilio_queue_new(test->io);
    t->queue[2] = grilio_queue_new(test->io);

    /* There are no requests with zero id */
    g_assert(!grilio_queue_cancel_request(t->queue[0], 0, FALSE));

    /* Test NULL resistance */
    g_assert(!grilio_queue_ref(NULL));
    g_assert(!grilio_queue_new(NULL));
    grilio_queue_cancel_request(NULL, 0, FALSE);
    grilio_queue_cancel_all(NULL, FALSE);

    /* First wait until we get connected to the test server */
    t->connected_id = grilio_channel_add_connected_handler(test->io,
        test_queue_start, t);

    /* Run the test */
    g_main_loop_run(test->loop);

    g_assert(t->last_id);
    grilio_queue_cancel_all(t->queue[0], FALSE);
    grilio_queue_cancel_all(t->queue[1], FALSE);
    grilio_queue_unref(t->queue[0]);
    grilio_queue_unref(t->queue[1]);
    g_assert(!t->queue[2]); /* This one should already be NULL */
    grilio_queue_unref(t->queue[2]);
    test_free(test);
}

/*==========================================================================*
 * Transaction1
 *==========================================================================*/

typedef struct test_transaction1_data {
    Test test;
    GRilIoQueue* queue[3];
    guint32 count;
} TestTransaction1;

static
void
test_transaction1_last_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction1* t = user_data;
    GDEBUG("Last response status %d", status);
    g_assert(status == GRILIO_STATUS_OK);

    /* All other response must have arrived by now */
    g_assert(t->count == G_N_ELEMENTS(t->queue));
    g_main_loop_quit(t->test.loop);
}

static
void
test_transaction1_handle_response(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction1* t = user_data;
    GRilIoParser parser;
    guint32 i;
    grilio_parser_init(&parser, data, len);
    g_assert(grilio_parser_get_uint32(&parser, &i));
    g_assert(grilio_parser_at_end(&parser));
    g_assert(t->count == i);
    t->count++;
    GDEBUG("Response %u received", i);
    /* Start the next transaction */
    g_assert(grilio_queue_transaction_state(t->queue[i]) ==
        GRILIO_TRANSACTION_STARTED);
    grilio_queue_transaction_finish(t->queue[i]);
}

static
void
test_transaction1(
    void)
{
    TestTransaction1* t = test_new(TestTransaction1, "Transaction1");
    Test* test = &t->test;
    int i;

    for (i=0; i<G_N_ELEMENTS(t->queue); i++) {
        GRILIO_TRANSACTION_STATE state;
        t->queue[i] = grilio_queue_new(test->io);
        g_assert(grilio_queue_transaction_state(t->queue[i]) ==
            GRILIO_TRANSACTION_NONE);
        state = grilio_queue_transaction_start(t->queue[i]);
        if (i == 0) {
            g_assert(state == GRILIO_TRANSACTION_STARTED);
        } else {
            g_assert(state == GRILIO_TRANSACTION_QUEUED);
        } 
        /* Second time makes no difference, the state doesn't change */
        g_assert(grilio_queue_transaction_start(t->queue[i]) == state);
        g_assert(grilio_queue_transaction_state(t->queue[i]) == state);
    }

    /* This one should be processed last */
    grilio_channel_send_request_full(test->io, NULL, RIL_REQUEST_TEST,
        test_transaction1_last_response, NULL, t);

    /* test_response_reflect_ok will send the same data back */
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST,
        test_response_reflect_ok, test);

    /* Submit requests in the opposite order */
    for (i=G_N_ELEMENTS(t->queue)-1; i>=0; i--) {
        GRilIoRequest* req = grilio_request_new();
        grilio_request_append_int32(req, i);
        grilio_queue_send_request_full(t->queue[i], req, RIL_REQUEST_TEST,
            test_transaction1_handle_response, NULL, t);
        grilio_request_unref(req);
    }

    /* Test NULL resistance */
    g_assert(grilio_channel_transaction_start(test->io, NULL) ==
        GRILIO_TRANSACTION_NONE);
    g_assert(grilio_channel_transaction_state(test->io, NULL) ==
        GRILIO_TRANSACTION_NONE);
    grilio_channel_transaction_finish(test->io, NULL);
    g_assert(grilio_queue_transaction_start(NULL) == GRILIO_TRANSACTION_NONE);
    g_assert(grilio_queue_transaction_state(NULL) == GRILIO_TRANSACTION_NONE);
    grilio_queue_transaction_finish(NULL);

    /* Run the test */
    g_main_loop_run(test->loop);

    for (i=0; i<G_N_ELEMENTS(t->queue); i++) {
        grilio_queue_unref(t->queue[i]);
    }
    test_free(test);
}

/*==========================================================================*
 * Transaction2
 *==========================================================================*/

typedef struct test_transaction2_data {
    Test test;
    int completion_count;
    GRilIoQueue* q;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* txreq1;
    GRilIoRequest* txreq2;
} TestTransaction2;

static
void
test_transaction2_txreq1_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction2* t = user_data;
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->txreq1) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->txreq2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_queue_transaction_state(t->q) ==
        GRILIO_TRANSACTION_STARTED);
    grilio_test_server_add_response_data(t->test.server, id,
        GRILIO_STATUS_OK, NULL, 0);
}

static
void
test_transaction2_txreq2_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction2* t = user_data;
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->txreq1) == GRILIO_REQUEST_SENT ||
             grilio_request_status(t->txreq1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->txreq2) == GRILIO_REQUEST_SENT);
    g_assert(grilio_queue_transaction_state(t->q) ==
        GRILIO_TRANSACTION_STARTED);
    grilio_test_server_add_response_data(t->test.server, id,
        GRILIO_STATUS_OK, NULL, 0);
    grilio_channel_transaction_finish(t->test.io, t->q);
}

static
void
test_transaction2_req_done(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction2* t = user_data;
    t->completion_count++;
    GDEBUG("Completion count %d", t->completion_count);
    g_assert(status == GRILIO_STATUS_OK);
    if (t->completion_count == 4) {
        g_main_loop_quit(t->test.loop);
    }
}

static
void
test_transaction2_req1_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    TestTransaction2* t = user_data;
    GRilIoChannel* io = t->test.io;

    g_assert(grilio_channel_transaction_start(io, t->q) ==
        GRILIO_TRANSACTION_STARTED);
    /* This one will wait for req1 to complete */
    grilio_queue_send_request_full(t->q, t->txreq1, RIL_REQUEST_TEST_2,
        test_transaction2_req_done, NULL, t);
    /* This one will wait for txreq1 to complete */
    grilio_channel_send_request_full(io, t->req2, RIL_REQUEST_TEST_3,
        test_transaction2_req_done, NULL, t);
    /* This one will get ahead of req2 because it's part of the transaction */
    grilio_queue_send_request_full(t->q, t->txreq2, RIL_REQUEST_TEST_4,
        test_transaction2_req_done, NULL, t);

    /* Assert the state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->txreq1) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->txreq2) == GRILIO_REQUEST_QUEUED);
    grilio_test_server_add_response_data(t->test.server, id,
        GRILIO_STATUS_OK, NULL, 0);
}

static
void
test_transaction2(
    void)
{
    TestTransaction2* t = test_new(TestTransaction2, "Transaction2");
    Test* test = &t->test;

    t->q = grilio_queue_new(test->io);
    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->txreq1 = grilio_request_new();
    t->txreq2 = grilio_request_new();

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_1,
        test_transaction2_req1_resp, t);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_transaction2_txreq1_resp, t);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_3,
        test_response_reflect_ok, test);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_4,
        test_transaction2_txreq2_resp, t);

    /* This one will be handled first */
    grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST_1,
        test_transaction2_req_done, NULL, t);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Assert the state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->txreq1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->txreq2) == GRILIO_REQUEST_DONE);
    g_assert(grilio_queue_transaction_state(t->q) == GRILIO_TRANSACTION_NONE);

    /* Cleanup */
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->txreq1);
    grilio_request_unref(t->txreq2);
    grilio_queue_unref(t->q);
    test_free(test);
}

/*==========================================================================*
 * WriteError
 *==========================================================================*/

static
void
test_write_error_handler(
    GRilIoChannel* io,
    const GError* error,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("%s", GERRMSG(error));
    g_main_loop_quit(test->loop);
}

static
void
test_write_completion(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    GDEBUG("Completion status %d", status);
}

static
void
test_write_error1_connected(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error_handler, test);
    grilio_channel_send_request(io, NULL, RIL_REQUEST_TEST);
}

static
void
test_write_error2_connected(
    GRilIoChannel* io,
    void* user_data)
{
    guint id;
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error_handler, test);
    id = grilio_channel_send_request(io, NULL, RIL_REQUEST_TEST);
    /* The first cancel should succeed, the second one fail */
    g_assert(grilio_channel_cancel_request(io, id, TRUE));
    g_assert(!grilio_channel_cancel_request(io, id, TRUE));
    /* There's no requests with zero id */
    g_assert(!grilio_channel_cancel_request(io, 0, TRUE));
}

static
void
test_write_error3_connected(
    GRilIoChannel* io,
    void* user_data)
{
    guint id;
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error_handler, test);
    id = grilio_channel_send_request_full(io, NULL,
        RIL_REQUEST_TEST, test_write_completion, NULL, test);
    /* The first cancel should succeed, the second one fail */
    g_assert(grilio_channel_cancel_request(io, id, TRUE));
    g_assert(!grilio_channel_cancel_request(io, id, TRUE));
    /* INT_MAX is a non-existent id */
    g_assert(!grilio_channel_cancel_request(io, INT_MAX, TRUE));
}

static
void
test_write_error(
    const char* name,
    GRilIoChannelEventFunc connected)
{
    Test* test = test_new(Test, name);
    grilio_channel_add_connected_handler(test->io, connected, test);
    g_main_loop_run(test->loop);
    test_free(test);
}

static
void
test_write_error1(
    void)
{
    /* grilio_channel_new_socket("/tmp" must fail */
    g_assert(!grilio_channel_new_socket("/tmp", NULL));
    test_write_error("WriteError1", test_write_error1_connected);
}

static
void
test_write_error2(
    void)
{
    test_write_error("WriteError2", test_write_error2_connected);
}

static
void
test_write_error3(
    void)
{
    test_write_error("WriteError3", test_write_error3_connected);
}

/*==========================================================================*
 * Disconnect
 *==========================================================================*/

static
void
test_disconnect_handler(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    g_main_loop_quit(test->loop);
}

static
void
test_disconnect(
    void)
{
    Test* test = test_new(Test, "EOF");
    grilio_channel_add_disconnected_handler(test->io,
        test_disconnect_handler, test);
    grilio_test_server_shutdown(test->server);
    g_main_loop_run(test->loop);
    test_free(test);
}

/*==========================================================================*
 * ShortPacket
 *==========================================================================*/

static
void
test_short_packet_handler(
    GRilIoChannel* io,
    const GError* error,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("%s", GERRMSG(error));
    g_main_loop_quit(test->loop);
}

static
void
test_short_packet(
    void)
{
    Test* test = test_new(Test, "EOF");
    static char data[2] = {0xff, 0xff};
    guint32 pktlen = GINT32_TO_BE(sizeof(data));
    grilio_channel_add_error_handler(test->io, test_short_packet_handler, test);
    grilio_test_server_add_data(test->server, &pktlen, sizeof(pktlen));
    grilio_test_server_add_data(test->server, data, sizeof(data));
    /* These two do nothing (but improve branch coverage): */
    grilio_channel_add_error_handler(NULL, test_short_packet_handler, NULL);
    grilio_channel_add_error_handler(test->io, NULL, NULL);
    g_main_loop_run(test->loop);
    test_free(test);
}

/*==========================================================================*
 * Logger
 *==========================================================================*/

typedef struct test_logger_data {
    Test test;
    guint test_log;
    guint bytes_in;
    guint bytes_out;
} TestLogger;

static
void
test_logger_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    GDEBUG("Response status %d", status);
}

static
void
test_logger_cb(
    GRilIoChannel* io,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    TestLogger* log = user_data;
    if (type == GRILIO_PACKET_REQ) {
        log->bytes_out += len;
        GDEBUG("%u bytes out (total %u)", len, log->bytes_out);
    } else {
        log->bytes_in += len;
        GDEBUG("%u bytes in (total %u)", len, log->bytes_in);
    }

    /*
     * Out:
     * 8 bytes RIL_REQUEST_TEST request
     *
     * In:
     * 16 bytes RIL_UNSOL_RIL_CONNECTED response
     * 32 bytes RIL_REQUEST_TEST
     */
    if (log->bytes_in == (16 + 32) && log->bytes_out == 8) {
        g_main_loop_quit(log->test.loop);
    }
}

static
void
test_logger(
    void)
{
    TestLogger* log = test_new(TestLogger, "Logger");
    Test* test = &log->test;
    guint id[3];
    int level = GLOG_LEVEL_ALWAYS;

    g_assert(!grilio_channel_add_logger(NULL, NULL, NULL));
    g_assert(!grilio_channel_add_logger(test->io, NULL, NULL));

    /* Remove default logger and re-add it with GLOG_LEVEL_ALWAYS, mainly
     * to improve code coverage. Remove it twice to make sure that invalid
     * logger ids are handled properly, i.e. ignored. */
    grilio_channel_remove_logger(test->io, test->log);
    grilio_channel_remove_logger(test->io, test->log);
    test->log = grilio_channel_add_default_logger(test->io, level);
    log->test_log = grilio_channel_add_logger(test->io, test_logger_cb, log);
    g_assert(test->log);
    g_assert(log->test_log);
    gutil_log(GLOG_MODULE_CURRENT, level, "%s", "");

    id[0] = test_basic_request(test, test_logger_response);
    id[1] = test_basic_request(test, test_logger_response);
    id[2] = test_basic_request(test, test_logger_response);
    grilio_channel_cancel_request(test->io, id[0], TRUE);
    grilio_channel_cancel_request(test->io, id[1], FALSE);
    g_assert(id[0]);
    g_assert(id[1]);
    g_assert(id[2]);
    g_assert(test_basic_response_ok(test->server, "LOGTEST", id[2]));

    /* Run the test */
    g_main_loop_run(test->loop);

    grilio_channel_remove_logger(test->io, log->test_log);
    test_free(test);
}

/*==========================================================================*
 * Handlers
 *==========================================================================*/

#define TEST_HANDLERS_COUNT (2)
#define TEST_HANDLERS_INC_EVENTS_COUNT (3)

enum test_handlers_event {
    TEST_HANDLERS_INC_EVENT = 1,
    TEST_HANDLERS_REMOVE_EVENT,
    TEST_HANDLERS_DONE_EVENT
};

typedef struct test_handlers_data {
    Test test;
    int count1;
    int count2;
    gulong next_event_id;
    gulong total_event_id;
    gulong id1[TEST_HANDLERS_COUNT];
    gulong id2[TEST_HANDLERS_COUNT];
} TestHandlers;

static
void
test_handlers_submit_event(
    Test* test,
    guint code)
{
    guint32 buf[3];
    buf[0] = GUINT32_TO_BE(8);          /* Length */
    buf[1] = GUINT32_TO_RIL(1);         /* Unsolicited Event */
    buf[2] = GUINT32_TO_RIL(code);      /* Event code */
    grilio_test_server_add_data(test->server, buf, sizeof(buf));
}

static void
test_handlers_done(
    GRilIoChannel* io,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Done");
    g_main_loop_quit(test->loop);
}

static void
test_handlers_remove(
    GRilIoChannel* io,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    TestHandlers* h = user_data;
    Test* test = &h->test;
    int i;

    g_assert(code == TEST_HANDLERS_REMOVE_EVENT);
    GDEBUG("Removing handlers");
    grilio_channel_remove_handlers(test->io, h->id2, TEST_HANDLERS_COUNT);

    /* Doing it a few more times makes no difference */
    grilio_channel_remove_handlers(test->io, h->id2, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handlers(test->io, h->id2, 0);
    grilio_channel_remove_handlers(test->io, NULL, 0);
    grilio_channel_remove_handlers(NULL, NULL, 0);

    /* Submit more events. This time they should only increment count1 */
    for (i=0; i<TEST_HANDLERS_INC_EVENTS_COUNT; i++) {
        test_handlers_submit_event(test, TEST_HANDLERS_INC_EVENT);
    }

    /* Once those are handled, stop the test */
    grilio_channel_remove_handler(test->io, h->next_event_id);
    h->next_event_id = grilio_channel_add_unsol_event_handler(test->io,
        test_handlers_done, TEST_HANDLERS_DONE_EVENT, h);
    test_handlers_submit_event(test, TEST_HANDLERS_DONE_EVENT);
}

static void
test_handlers_inc(
    GRilIoChannel* io,
    guint code,
    const void* data,
    guint len,
    void* user_data)
{
    int* count = user_data;
    (*count)++;
    GDEBUG("Event %u data %p value %d", code, count, *count);
}

static
void
test_handlers(
    void)
{
    TestHandlers* h = test_new(TestHandlers, "Handlers");
    Test* test = &h->test;
    int i, total = 0;

    /* Prepare the test */
    for (i=0; i<TEST_HANDLERS_COUNT; i++) {
        h->id1[i] = grilio_channel_add_unsol_event_handler(test->io,
            test_handlers_inc, TEST_HANDLERS_INC_EVENT, &h->count1);
        h->id2[i] = grilio_channel_add_unsol_event_handler(test->io,
            test_handlers_inc, TEST_HANDLERS_INC_EVENT, &h->count2);
    }
    h->total_event_id = grilio_channel_add_unsol_event_handler(test->io,
        test_handlers_inc, 0, &total);
    for (i=0; i<TEST_HANDLERS_INC_EVENTS_COUNT; i++) {
        test_handlers_submit_event(test, TEST_HANDLERS_INC_EVENT);
    }
    h->next_event_id = grilio_channel_add_unsol_event_handler(test->io,
        test_handlers_remove, TEST_HANDLERS_REMOVE_EVENT, h);
    test_handlers_submit_event(test, TEST_HANDLERS_REMOVE_EVENT);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(h->count1 == 2*TEST_HANDLERS_COUNT*TEST_HANDLERS_INC_EVENTS_COUNT);
    g_assert(h->count2 == TEST_HANDLERS_COUNT*TEST_HANDLERS_INC_EVENTS_COUNT);
    /* Total count includes RIL_UNSOL_RIL_CONNECTED + REMOVE and DONE */
    g_assert(total == TEST_HANDLERS_COUNT*TEST_HANDLERS_INC_EVENTS_COUNT + 3);
    for (i=0; i<TEST_HANDLERS_COUNT; i++) {
        g_assert(!h->id2[i]);
    }

    /* Clean up */
    grilio_channel_remove_handlers(test->io, h->id1, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handlers(test->io, h->id2, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handler(test->io, h->total_event_id);
    grilio_channel_remove_handler(test->io, h->next_event_id);
    /* These do nothing: */
    grilio_channel_remove_handler(test->io, 0);
    grilio_channel_remove_handler(NULL, 0);
    test_free(test);
}

/*==========================================================================*
 * EarlyResp
 *==========================================================================*/

static
void
test_early_resp_no_completion(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    int* resp_count = user_data;
    GDEBUG("Request 2 completed with status %d", status);
    g_assert(status == GRILIO_STATUS_CANCELLED);
    (*resp_count)++;
}

static
void
test_early_resp_req2_completion(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 2 completed with status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    g_main_loop_quit(test->loop);
}

static
void
test_early_resp(
    void)
{
    Test* test = test_new(Test, "EarlyResp");
    GRilIoRequest* req = grilio_request_new();
    int resp_count = 0;

    /* This one is going to end the test (eventually). */
    const guint id1 = grilio_channel_send_request_full(test->io, NULL,
        RIL_REQUEST_TEST, test_early_resp_req2_completion, NULL, test);
    /* Response to this request will arrive before the request has been
     * sent, so it will be ignored. */
    const guint id2 = grilio_channel_send_request_full(test->io, req,
        RIL_REQUEST_TEST, test_early_resp_no_completion, NULL, &resp_count);

    g_assert(test_basic_response_ok(test->server, "IGNORE", id2));
    g_assert(test_basic_response_ok(test->server, "DONE", id1));

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the request state. It should be SENT */
    g_assert(grilio_request_status(req) == GRILIO_REQUEST_SENT);
    g_assert(!resp_count);
    grilio_channel_cancel_all(test->io, TRUE);
    g_assert(resp_count == 1);
    grilio_request_unref(req);
    test_free(test);
}

/*==========================================================================*
 * Retry1
 *
 * We create 3 requests - #1 with request timeout and retry, #2 with one
 * retries and no request timeout and #3 with infinite number of retries
 * and no request timeout.
 *
 * Then we send error reply to #2 and #3, wait for #1 to time out, send
 * themselves another request to make sure that #2 and #3 have received
 * their replies, send another error replies to #3 and #2, wait for #2 to
 * complete with error and cancel #3. We expect reply count for #3 to be 1
 *==========================================================================*/

typedef struct test_retry1_data {
    Test test;
    int req2_status;
    int req3_completed;
    int req4_completed;
    int req5_completed;
    int req6_completed;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
    GRilIoRequest* req4;
    GRilIoRequest* req5;
    GRilIoRequest* req6;
} TestRetry1;

static
void
test_retry1_continue(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestRetry1* retry = G_CAST(test, TestRetry1, test);

    /* This should result in req2 getting completed */
    GDEBUG("Continuing...");
    grilio_test_server_add_response(test->server, NULL,
        retry->req2->current_id, RIL_E_REQUEST_NOT_SUPPORTED);
}

static
void
test_retry1_req1_timeout(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 1 completed with status %d", status);
    if (status == GRILIO_STATUS_TIMEOUT) {
        test_basic_response_ok(test->server, "TEST",
            test_basic_request(test, test_retry1_continue));
    }
}

static
gboolean
test_retry1_req2_retry(
    GRilIoRequest* request,
    int ril_status,
    const void* response_data,
    guint response_len,
    void* user_data)
{
    return ril_status == RIL_E_GENERIC_FAILURE;
}

static
void
test_retry1_req2_completion(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestRetry1* retry = G_CAST(test, TestRetry1, test);
    retry->req2_status = status;
    GDEBUG("Request 2 completed with status %d", status);
    if (!retry->req3_completed) {
        /* First cancel should succeed, the second one fail */
        guint id3 = grilio_request_id(retry->req3);
        if (grilio_channel_cancel_request(test->io, id3, TRUE) &&
            !grilio_channel_cancel_request(test->io, id3, TRUE)) {
            if (retry->req3_completed) {
                g_main_loop_quit(test->loop);
            }
        }
    }
}

static
void
test_retry1_count_completions(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    int* completed = user_data;
    (*completed)++;
}

static
void
test_retry1_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestRetry1* retry = G_CAST(test, TestRetry1, test);
    GRilIoQueue* q;

    grilio_channel_send_request_full(test->io, retry->req1,
        RIL_REQUEST_TEST, test_retry1_req1_timeout, NULL, test);
    grilio_channel_send_request_full(test->io, retry->req2,
        RIL_REQUEST_TEST, test_retry1_req2_completion, NULL, test);
    grilio_channel_send_request_full(test->io, retry->req3,
        RIL_REQUEST_TEST, test_retry1_count_completions, NULL,
        &retry->req3_completed);
    grilio_channel_send_request_full(test->io, retry->req4,
        RIL_REQUEST_TEST, test_retry1_count_completions, NULL,
        &retry->req4_completed);
    grilio_channel_send_request_full(test->io, retry->req5,
        RIL_REQUEST_TEST, test_retry1_count_completions, NULL,
        &retry->req5_completed);
    grilio_channel_send_request_full(test->io, retry->req6,
        RIL_REQUEST_TEST, test_retry1_count_completions, NULL,
        &retry->req6_completed);

    /* Can't send the same request twice */
    q = grilio_queue_new(test->io);
    g_assert(!grilio_queue_send_request(q, retry->req6, RIL_REQUEST_TEST));
    grilio_queue_unref(q);
    g_assert(!grilio_channel_send_request(test->io, retry->req6,
        RIL_REQUEST_TEST));
    g_assert(grilio_channel_get_request(test->io,
        grilio_request_id(retry->req6)) == retry->req6);

    grilio_test_server_add_response(test->server, NULL,
        retry->req2->current_id, RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        retry->req3->current_id, RIL_E_REQUEST_NOT_SUPPORTED);
    grilio_test_server_add_response(test->server, NULL,
        retry->req4->current_id, RIL_E_REQUEST_NOT_SUPPORTED);
    grilio_test_server_add_response(test->server, NULL,
        retry->req5->current_id, RIL_E_REQUEST_NOT_SUPPORTED);
    grilio_test_server_add_response(test->server, NULL,
        retry->req6->current_id, RIL_E_REQUEST_NOT_SUPPORTED);
    /* And wait for req1 to timeout */
}

static
void
test_retry1(
    void)
{
    TestRetry1* retry = test_new(TestRetry1, "Retry1");
    Test* test = &retry->test;

    /* Prepare the test */
    retry->req1 = grilio_request_new();
    retry->req2 = grilio_request_new();
    retry->req3 = grilio_request_new();
    retry->req4 = grilio_request_new();
    retry->req5 = grilio_request_new();
    retry->req6 = grilio_request_new();

    grilio_request_set_retry(retry->req1, 10, 1);
    grilio_request_set_timeout(retry->req1, 10);
    grilio_request_set_retry(retry->req2, 0, 1);
    grilio_request_set_retry_func(retry->req2, test_retry1_req2_retry);
    grilio_request_set_retry(retry->req3, 0, -1);
    grilio_request_set_retry(retry->req4, INT_MAX-1, -1);
    grilio_request_set_retry(retry->req5, INT_MAX, -1);
    grilio_request_set_retry(retry->req6, INT_MAX, -1);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry1_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_status(retry->req4) == GRILIO_REQUEST_RETRY);
    g_assert(grilio_request_status(retry->req5) == GRILIO_REQUEST_RETRY);
    g_assert(grilio_request_status(retry->req6) == GRILIO_REQUEST_RETRY);

    grilio_channel_cancel_request(test->io,
        grilio_request_id(retry->req5), TRUE);
    grilio_channel_cancel_request(test->io,
        grilio_request_id(retry->req4), TRUE);
    grilio_channel_cancel_all(test->io, TRUE);

    g_assert(grilio_request_status(retry->req4) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(retry->req5) == GRILIO_REQUEST_CANCELLED);
    g_assert(retry->req2_status == RIL_E_REQUEST_NOT_SUPPORTED);
    g_assert(retry->req3_completed == 1);
    g_assert(retry->req4_completed == 1);
    g_assert(grilio_request_retry_count(retry->req1) == 1);
    g_assert(grilio_request_retry_count(retry->req2) == 1);
    g_assert(grilio_request_retry_count(retry->req3) == 1);
    g_assert(grilio_request_retry_count(retry->req4) == 0);

    /* Clean up */
    grilio_request_unref(retry->req1);
    grilio_request_unref(retry->req2);
    grilio_request_unref(retry->req3);
    grilio_request_unref(retry->req4);
    grilio_request_unref(retry->req5);
    grilio_request_unref(retry->req6);
    test_free(test);
}

/*==========================================================================*
 * Retry2
 *
 * Makes sure that request ids are incremented on each retry. Two requests
 * are rejected, the third one times out.
 *==========================================================================*/

typedef struct test_retry2_data {
    Test test;
    guint log_id;
    guint req_id;
    GRilIoRequest* req;
} TestRetry2;

static
void
test_retry2_req_done(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestRetry2* retry = G_CAST(test, TestRetry2, test);
    GDEBUG("Request %u completed with status %d",
        grilio_request_id(retry->req), status);
    if (status == RIL_E_GENERIC_FAILURE) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_retry2_log(
    GRilIoChannel* channel,
    GRILIO_PACKET_TYPE type,
    guint id,
    guint code,
    const void* data,
    guint data_len,
    void* user_data)
{
    Test* test = user_data;
    TestRetry2* retry = G_CAST(test, TestRetry2, test);
    if (type == GRILIO_PACKET_REQ) {
        if (!retry->req_id) {
            GDEBUG("No request id yet, assuming %u", id);
            retry->req_id = id;
        } else {
            retry->req_id++;
            g_assert(retry->req_id == id);
        }
        if (retry->req_id == id) {
            GDEBUG("Failing request %u", id);
            grilio_test_server_add_response(test->server, NULL, id,
                RIL_E_GENERIC_FAILURE);
        }
    }
}

static
void
test_retry2_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestRetry2* retry = G_CAST(test, TestRetry2, test);

    retry->req_id = grilio_channel_send_request_full(test->io, retry->req,
        RIL_REQUEST_TEST, test_retry2_req_done, NULL, test);
}

static
void
test_retry2(
    void)
{
    TestRetry2* retry = test_new(TestRetry2, "Retry2");
    Test* test = &retry->test;
    guint id;

    /* Prepare the test */
    retry->log_id = grilio_channel_add_logger(test->io, test_retry2_log, test);
    retry->req = grilio_request_new();
    grilio_request_set_retry(retry->req, 10, 2);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry2_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_retry_count(retry->req) == 2);

    /* Clean up */
    id = grilio_request_id(retry->req);
    g_assert(grilio_request_status(retry->req) == GRILIO_REQUEST_DONE);
    /* Since it's done, cancel will fail */
    g_assert(!grilio_channel_cancel_request(test->io, id, FALSE));
    grilio_request_unref(retry->req);
    grilio_channel_remove_logger(test->io, retry->log_id);
    test_free(test);
}

/*==========================================================================*
 * Retry3
 *
 * Makes sure that we don't retry pending request.
 *
 * 1. Create request with a long response and retry timeouts and 1 retry
 * 2. Reply to it with an error, it gets scheduled for retry
 * 3. Call grilio_channel_retry_request, it should be retried immediately
 * 4. Call grilio_channel_retry_request again. It should fail.
 *
 * The same thing is done with 2 requests to improve the coverage.
 *
 *==========================================================================*/

typedef struct test_retry3_data {
    Test test;
    guint dummy_id;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    int status1;
    int status2;
} TestRetry3;

static
void
test_retry3_req_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    int* completion_status = user_data;
    GDEBUG("Request completed with status %d", status);
    *completion_status = status;
}

static
void
test_retry3_check_req(
    GRilIoChannel* io,
    GRilIoRequest* req,
    guint dummy_id)
{
    const guint id = grilio_request_id(req);
    g_assert(!grilio_channel_retry_request(NULL, dummy_id));
    g_assert(!grilio_channel_retry_request(io, dummy_id));
    g_assert(!grilio_channel_retry_request(io, 0));
    g_assert(grilio_channel_retry_request(io, id));
    g_assert(!grilio_channel_retry_request(io, id)); /* Must fail */
    g_assert(!grilio_channel_retry_request(io, dummy_id));
    g_assert(grilio_channel_cancel_request(io, id, TRUE));
}

static
void
test_retry3_retry_and_cancel(
    GRilIoChannel* io,
    int error,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestRetry3* retry = G_CAST(test, TestRetry3, test);
    if (error) {
        GDEBUG("Unexpected completion status %d", error);
    } else {
        test_retry3_check_req(io, retry->req1, retry->dummy_id);
        test_retry3_check_req(io, retry->req2, retry->dummy_id);
        g_main_loop_quit(test->loop);
    }
}

static
void
test_retry3_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestRetry3* retry = G_CAST(test, TestRetry3, test);

    grilio_channel_send_request_full(test->io, retry->req1,
        RIL_REQUEST_TEST, test_retry3_req_completed, NULL, &retry->status1);
    grilio_channel_send_request_full(test->io, retry->req2,
        RIL_REQUEST_TEST, test_retry3_req_completed, NULL, &retry->status2);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(retry->req1), RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(retry->req2), RIL_E_GENERIC_FAILURE);

    /* By the time this request completes, the first one should already
     * be in the retry queue */
    retry->dummy_id = test_basic_request(test, test_retry3_retry_and_cancel);
    test_basic_response_ok(test->server, NULL, retry->dummy_id);
}

static
void
test_retry3(
    void)
{
    TestRetry3* retry = test_new(TestRetry3, "Retry3");
    Test* test = &retry->test;
    guint id1, id2;

    /* Prepare the test */
    retry->req1 = grilio_request_new();
    retry->req2 = grilio_request_new();
    grilio_request_set_timeout(retry->req1, INT_MAX);
    grilio_request_set_timeout(retry->req2, INT_MAX);
    grilio_request_set_retry(retry->req1, INT_MAX, 1);
    grilio_request_set_retry(retry->req2, INT_MAX-1, 1);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry3_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_retry_count(retry->req1) == 1);
    g_assert(grilio_request_retry_count(retry->req2) == 1);
    g_assert(retry->status1 == GRILIO_STATUS_CANCELLED);
    g_assert(retry->status2 == GRILIO_STATUS_CANCELLED);

    id1 = grilio_request_id(retry->req1);
    id2 = grilio_request_id(retry->req2);
    grilio_channel_cancel_request(test->io, id1, FALSE);
    grilio_channel_cancel_request(test->io, id2, FALSE);
    grilio_request_unref(retry->req1);
    grilio_request_unref(retry->req2);
}

/*==========================================================================*
 * Timeout1
 *==========================================================================*/

typedef struct test_timeout1_data {
    Test test;
    int timeout_count;
    guint req_id;
    guint timer_id;
} TestTimeout1;

static
gboolean
test_timeout1_done(
    gpointer user_data)
{
    Test* test = user_data;
    TestTimeout1* timeout = G_CAST(test, TestTimeout1, test);
    timeout->timer_id = 0;
    GDEBUG("Cancelling request %u", timeout->req_id);
    if (grilio_channel_cancel_request(test->io, timeout->req_id, TRUE) &&
        timeout->timeout_count == 1) {
        g_main_loop_quit(test->loop);
    }
    return FALSE;
}

static
void
test_timeout1_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestTimeout1* timeout = G_CAST(test, TestTimeout1, test);
    GDEBUG("Completion status %d", status);
    if (status == GRILIO_STATUS_TIMEOUT) {
        timeout->timeout_count++;
        if (!timeout->timer_id) {
            timeout->timer_id = g_timeout_add(200, test_timeout1_done, test);
        }
    }
}

static
void
test_timeout1_submit_requests(
    Test* test,
    GRilIoChannelResponseFunc fn)
{
    TestTimeout1* timeout = G_CAST(test, TestTimeout1, test);
    GRilIoRequest* req1 = grilio_request_new();
    GRilIoRequest* req2 = grilio_request_new();
    grilio_channel_set_timeout(test->io, 10);
    grilio_request_set_timeout(req2, INT_MAX);

    grilio_channel_send_request_full(test->io, req1,
        RIL_REQUEST_TEST, fn, NULL, test);
    timeout->req_id = grilio_channel_send_request_full(test->io, req2,
        RIL_REQUEST_TEST, test_timeout1_response, NULL, test);
    grilio_request_unref(req1);
    grilio_request_unref(req2);
}

static
void
test_timeout1_start(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestTimeout1* timeout = G_CAST(test, TestTimeout1, test);
    if (status == GRILIO_STATUS_TIMEOUT) {
        GDEBUG("Starting...");
        if (grilio_channel_cancel_request(test->io, timeout->req_id, FALSE)) {
            test_timeout1_submit_requests(test, test_timeout1_response);
        }
    }
}

static
void
test_timeout1(
    void)
{
    TestTimeout1* timeout = test_new(TestTimeout1, "Timeout1");
    Test* test = &timeout->test;
    test_timeout1_submit_requests(test, test_timeout1_start);
    g_assert(!timeout->timer_id);
    test_free(test);
}

/*==========================================================================*
 * Timeout2
 *==========================================================================*/

typedef struct test_timeout2_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
} TestTimeout2;

static
void
test_timeout2_req2_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 2 completion status %d", status);
    g_assert(status == GRILIO_STATUS_TIMEOUT);
    g_main_loop_quit(test->loop);
}

static
void
test_timeout2_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestTimeout2* ts = G_CAST(test, TestTimeout2, test);
    GDEBUG("Starting...");
    grilio_channel_send_request(test->io, ts->req1, RIL_REQUEST_TEST);
    grilio_channel_send_request_full(test->io, ts->req2,
        RIL_REQUEST_TEST, test_timeout2_req2_completed, NULL, test);
}

static
void
test_timeout2(
    void)
{
    TestTimeout2* t = test_new(TestTimeout2, "Timeout2");
    Test* test = &t->test;
    guint serial_id;

    /* Prepare the test */
    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    grilio_channel_set_timeout(test->io, 10);
    grilio_request_set_timeout(t->req2, 20);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    serial_id = grilio_channel_serialize(test->io);
    g_assert(serial_id);
    grilio_channel_add_connected_handler(test->io, test_timeout2_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    grilio_channel_deserialize(test->io, serial_id);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    test_free(test);
}

/*==========================================================================*
 * Serialize1
 *==========================================================================*/

typedef struct test_serialize1_data {
    Test test;
    guint serial_id;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
    GRilIoRequest* req4;
    GRilIoRequest* req5;
} TestSerialize1;

static
void
test_serialize1_req1_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize1* t = G_CAST(test, TestSerialize1, test);
    GDEBUG("Request 1 completion status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    /* Request has completed, retry fails */
    g_assert(!grilio_channel_retry_request(test->io,
        grilio_request_id(t->req1)));
    /* Request 2 should now be pending, so retry says TRUE */
    g_assert(grilio_channel_retry_request(test->io,
        grilio_request_id(t->req2)));
    /* The second request is sent before this callback is invoked,
     * cancel the third one */
    g_assert(grilio_channel_cancel_request(test->io,
        grilio_request_id(t->req3), FALSE));
    /* This one gets ignored: */
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(t->req3), GRILIO_STATUS_OK);
}

static
void
test_serialize1_req2_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize1* t = G_CAST(test, TestSerialize1, test);
    grilio_test_server_add_response(test->server, NULL, id,
        RIL_E_GENERIC_FAILURE);
    /* De-serialize the channel */
    grilio_channel_deserialize(test->io, t->serial_id);
}

static
void
test_serialize1_req2_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize1* t = G_CAST(test, TestSerialize1, test);
    GDEBUG("Request 2 completion status %d", status);
    g_assert(status == RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(t->req4), RIL_E_REQUEST_NOT_SUPPORTED);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(t->req5), GRILIO_STATUS_OK);
}

static
void
test_serialize1_req3_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    GDEBUG("Request 3 completion status %d", status);
    g_assert(status == GRILIO_STATUS_CANCELLED);
}

static
void
test_serialize1_req4_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    GDEBUG("Request 4 completion status %d", status);
    g_assert(status == RIL_E_REQUEST_NOT_SUPPORTED);
}

static
void
test_serialize1_req5_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 5 completion status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    g_main_loop_quit(test->loop);
}

static
void
test_serialize1_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize1* t = G_CAST(test, TestSerialize1, test);
    GDEBUG("Starting...");
    grilio_request_set_blocking(t->req2, TRUE);
    grilio_channel_send_request_full(test->io, t->req2,
        RIL_REQUEST_TEST_2, test_serialize1_req2_completed, NULL, test);
    grilio_channel_send_request_full(test->io, t->req3,
        RIL_REQUEST_TEST, test_serialize1_req3_completed, NULL, test);
    grilio_channel_send_request_full(test->io, t->req4,
        RIL_REQUEST_TEST, test_serialize1_req4_completed, NULL, test);
    grilio_channel_send_request_full(test->io, t->req5,
        RIL_REQUEST_TEST, test_serialize1_req5_completed, NULL, test);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(t->req1), GRILIO_STATUS_OK);
}

static
void
test_serialize1(
    void)
{
    TestSerialize1* t = test_new(TestSerialize1, "Serialize1");
    Test* test = &t->test;
    guint id;

    /* Prepare the test */
    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();
    t->req4 = grilio_request_new();
    t->req5 = grilio_request_new();
    grilio_channel_set_timeout(test->io, GRILIO_TIMEOUT_DEFAULT);
    grilio_request_set_timeout(t->req1, INT_MAX);

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_serialize1_req2_resp, test);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    t->serial_id = grilio_channel_serialize(test->io);
    g_assert(t->serial_id);
    /* Increment/decrement serialization count */
    grilio_channel_deserialize(test->io, grilio_channel_serialize(test->io));
    /* Submit the first request. There's nothing to retry, just make sure
     * that grilio_channel_retry_request returns TRUE. */
    id = grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST,
        test_serialize1_req1_completed, NULL, test);
    g_assert(grilio_channel_retry_request(test->io, id));
    /* And wait for the channel to get connected */
    grilio_channel_add_connected_handler(test->io,
        test_serialize1_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req5) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    grilio_request_unref(t->req4);
    grilio_request_unref(t->req5);
    grilio_channel_deserialize(test->io, t->serial_id); // No effect
    grilio_channel_deserialize(test->io, 0); // No effect
    test_free(test);
}

/*==========================================================================*
 * Serialize2
 *==========================================================================*/

typedef struct test_serialize2_data {
    Test test;
    guint serial_id;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
} TestSerialize2;

static
void
test_serialize2_req3_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize2* t = G_CAST(test, TestSerialize2, test);
    grilio_test_server_add_response(test->server, NULL, id,
        RIL_E_GENERIC_FAILURE);
    /* De-serialize the channel */
    grilio_channel_deserialize(test->io, t->serial_id);
}

static
void
test_serialize2_req1_resp(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize2* t = G_CAST(test, TestSerialize2, test);
    g_assert(grilio_request_id(t->req1) == id);
    grilio_test_server_add_response(test->server, NULL, id, GRILIO_STATUS_OK);
    /* Request 2 should is now pending, cancel it */
    g_assert(grilio_channel_cancel_request(test->io,
        grilio_request_id(t->req2), TRUE));
}

static
void
test_serialize2_req3_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 3 completion status %d", status);
    g_assert(status == RIL_E_GENERIC_FAILURE);
    g_main_loop_quit(test->loop);
}

static
void
test_serialize2_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize2* t = G_CAST(test, TestSerialize2, test);
    GDEBUG("Starting...");
    grilio_channel_send_request(test->io, t->req2, RIL_REQUEST_TEST);
    grilio_channel_send_request_full(test->io, t->req3, RIL_REQUEST_TEST_3,
        test_serialize2_req3_completed, NULL, test);
}

static
void
test_serialize2(
    void)
{
    TestSerialize2* t = test_new(TestSerialize2, "Serialize2");
    Test* test = &t->test;
    guint id;

    /* Prepare the test */
    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST,
        test_response_reflect_ok, test);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_1,
        test_serialize2_req1_resp, test);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_3,
        test_serialize2_req3_resp, test);

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    t->serial_id = grilio_channel_serialize(test->io);
    g_assert(t->serial_id);
    /* Submit the first request. There's nothing to retry, just make sure
     * that grilio_channel_retry_request returns TRUE. */
    id = grilio_channel_send_request(test->io, t->req1, RIL_REQUEST_TEST_1);
    g_assert(grilio_channel_retry_request(test->io, id));
    /* And wait for the channel to get connected */
    grilio_channel_add_connected_handler(test->io,
        test_serialize2_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    test_free(test);
}

/*==========================================================================*
 * Serialize3
 *==========================================================================*/

typedef struct test_serialize3_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
} TestSerialize3;

static
void
test_serialize3_req1_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize3* t = G_CAST(test, TestSerialize3, test);
    GDEBUG("Request 1 completion status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    g_assert(!grilio_channel_get_request(test->io,
        grilio_request_id(t->req1)));
    g_assert(grilio_channel_get_request(test->io,
        grilio_request_id(t->req2)) == t->req2);
    grilio_channel_cancel_all(test->io, TRUE);
}

static
void
test_serialize3_req3_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("Request 3 completion status %d", status);
    g_assert(status == GRILIO_STATUS_CANCELLED);
    g_main_loop_quit(test->loop);
}

static
void
test_serialize3_start(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    TestSerialize3* t = G_CAST(test, TestSerialize3, test);
    GDEBUG("Starting...");

    grilio_channel_send_request(test->io, t->req2, RIL_REQUEST_TEST);
    grilio_channel_send_request_full(test->io, t->req3,
        RIL_REQUEST_TEST, test_serialize3_req3_completed, NULL, test);
    grilio_test_server_add_response(test->server, NULL,
        grilio_request_id(t->req1), GRILIO_STATUS_OK);

    g_assert(grilio_channel_get_request(test->io,
        grilio_request_id(t->req1)) == t->req1);
    g_assert(grilio_channel_get_request(test->io,
        grilio_request_id(t->req2)) == t->req2);
    g_assert(grilio_channel_get_request(test->io,
        grilio_request_id(t->req3)) == t->req3);
}

static
void
test_serialize3(
    void)
{
    TestSerialize3* t = test_new(TestSerialize3, "Serialize3");
    Test* test = &t->test;
    guint serial_id;

    /* Prepare the test */
    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();

    /* Start after we have been connected */
    g_assert(!test->io->connected);
    serial_id = grilio_channel_serialize(test->io);
    g_assert(serial_id);
    grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST,
        test_serialize3_req1_completed, NULL, test);
    /* And Wait for the channel to get connected */
    grilio_channel_add_connected_handler(test->io,
        test_serialize3_start, test);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* Check the final state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_CANCELLED);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    grilio_channel_deserialize(test->io, serial_id);
    test_free(test);
}

/*==========================================================================*
 * Block1
 *==========================================================================*/

typedef struct test_block1_data {
    Test test;
    GRilIoRequest* req1; /* Blocking, cancelled before it's sent */
    GRilIoRequest* req2; /* Blocking, cancelled after it's sent */
    GRilIoRequest* req3; /* Blocking, should complete normally */
    GRilIoRequest* req4; /* Non-blocking, should complete normally */
    gboolean req3_done;
    gboolean req4_done;
} TestBlock1;

static
void
test_block1_req3_done(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlock1* t = user_data;
    g_assert(status == GRILIO_STATUS_OK);
    g_assert(!t->req3_done);
    g_assert(!t->req4_done);
    t->req3_done = TRUE;
}

static
void
test_block1_req4_done(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlock1* t = user_data;
    g_assert(status == GRILIO_STATUS_OK);
    g_assert(t->req3_done);
    g_assert(!t->req4_done);
    t->req4_done = TRUE;
    g_main_loop_quit(t->test.loop);
}

static
void
test_block1_resp2(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlock1* t = user_data;
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_id(t->req2) == id);
    /* This one is pending, retry says FALSE  */
    g_assert(!grilio_channel_retry_request(t->test.io, id));
    /* This one is queued, retry says TRUE */
    g_assert(grilio_channel_retry_request(t->test.io,
        grilio_request_id(t->req3)));
    /* Cancel this request */
    g_assert(grilio_channel_cancel_request(t->test.io, id, FALSE));
    /* And assert that request status has changed */
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    /* Send the response to unblock the channel */
    grilio_test_server_add_response_data(t->test.server, id,
        GRILIO_STATUS_OK, NULL, 0);
}

static
void
test_block1_resp3(
    guint code,
    guint id,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlock1* t = user_data;
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_QUEUED);
    grilio_test_server_add_response_data(t->test.server, id,
        GRILIO_STATUS_OK, NULL, 0);
}

static
void
test_block1_connected(
    GRilIoChannel* io,
    void* user_data)
{
    TestBlock1* t = user_data;
    /* Nothing has been sent yet */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_QUEUED);
    /* Cancel this one before it's been sent */
    g_assert(grilio_channel_cancel_request(t->test.io,
        grilio_request_id(t->req1), FALSE));
}

static
void
test_block1(
    void)
{
    TestBlock1* t = test_new(TestBlock1, "Block1");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();
    t->req4 = grilio_request_new();

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_block1_resp2, t);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_3,
        test_block1_resp3, t);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_4,
        test_response_empty_ok, t);

    grilio_request_set_blocking(t->req1, TRUE);
    grilio_channel_send_request(test->io, t->req1, RIL_REQUEST_TEST_1);
    grilio_request_set_blocking(t->req2, TRUE);
    grilio_channel_send_request(test->io, t->req2, RIL_REQUEST_TEST_2);
    grilio_request_set_blocking(t->req3, TRUE);
    grilio_channel_send_request_full(test->io, t->req3, RIL_REQUEST_TEST_3,
        test_block1_req3_done, NULL, t);
    grilio_channel_send_request_full(test->io, t->req4, RIL_REQUEST_TEST_4,
        test_block1_req4_done, NULL, t);

    /* Run the test */
    grilio_channel_add_connected_handler(test->io, test_block1_connected, t);
    g_main_loop_run(test->loop);

    g_assert(t->req3_done);
    g_assert(t->req4_done);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    grilio_request_unref(t->req4);
    test_free(test);
}

/*==========================================================================*
 * Block2
 *==========================================================================*/

typedef struct test_block2_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
    GRilIoRequest* req4;
} TestBlock2;

static
gboolean
test_block2_cancel_req1(
    void* user_data)
{
    TestBlock2* t = user_data;
    /* By now req1 should have been sent */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_QUEUED);
    /* Cancel the last one first */
    g_assert(grilio_channel_cancel_request(t->test.io,
        grilio_request_id(t->req4), TRUE));
    /* Cancel everything else */
    grilio_channel_cancel_all(t->test.io, FALSE);
    /* And assert that its status has changed */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_main_loop_quit(t->test.loop);
    return G_SOURCE_REMOVE;
}

static
void
test_block2_connected(
    GRilIoChannel* io,
    void* user_data)
{
    TestBlock2* t = user_data;
    /* Nothing has been sent yet */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_QUEUED);
    /* Will cancel req1 after it's been sent */
    g_assert(g_idle_add(test_block2_cancel_req1, t));
}

static
void
test_block2(
    void)
{
    TestBlock2* t = test_new(TestBlock2, "Block2");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();
    t->req4 = grilio_request_new();

    grilio_request_set_blocking(t->req1, TRUE);
    grilio_channel_send_request(test->io, t->req1, RIL_REQUEST_TEST);
    grilio_request_set_blocking(t->req2, TRUE);
    grilio_channel_send_request(test->io, t->req2, RIL_REQUEST_TEST);
    grilio_channel_send_request(test->io, t->req3, RIL_REQUEST_TEST);
    grilio_channel_send_request(test->io, t->req4, RIL_REQUEST_TEST);

    /* Run the test */
    grilio_channel_add_connected_handler(test->io, test_block2_connected, t);
    g_main_loop_run(test->loop);

    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req4) == GRILIO_REQUEST_CANCELLED);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    grilio_request_unref(t->req4);
    test_free(test);
}

/*==========================================================================*
 * BlockTimeout
 *==========================================================================*/

typedef struct test_block_timeout_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
} TestBlockTimeout;

static
void
test_block_timeout_req2_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlockTimeout* t = user_data;
    GDEBUG("Request 2 completion status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    g_main_loop_quit(t->test.loop);
}

static
void
test_block_timeout_req1_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestBlockTimeout* t = user_data;
    GDEBUG("Request 1 completion status %d", status);
    g_assert(status == GRILIO_STATUS_TIMEOUT);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
}

static
void
test_block_timeout(
    void)
{
    TestBlockTimeout* t = test_new(TestBlockTimeout, "BlockTimeout");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();

    /* Reply to the second one, let the first one to time out */
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_response_empty_ok, test);

    grilio_request_set_blocking(t->req1, TRUE);
    grilio_request_set_timeout(t->req1, 10);
    grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST_1,
        test_block_timeout_req1_completed, NULL, t);
    grilio_channel_send_request_full(test->io, t->req2, RIL_REQUEST_TEST_2,
        test_block_timeout_req2_completed, NULL, t);

    /* Run the test */
    g_main_loop_run(test->loop);

    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    test_free(test);
}

/*==========================================================================*
 * PendingTimeout
 *==========================================================================*/

typedef struct test_pending_timeout_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
} TestPendingTimeout;

static
void
test_pending_timeout_req2_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestPendingTimeout* t = user_data;
    GDEBUG("Request 2 completion status %d", status);
    g_assert(status == GRILIO_STATUS_OK);
    g_main_loop_quit(t->test.loop);
}

static
void
test_pending_timeout(
    void)
{
    TestPendingTimeout* t = test_new(TestPendingTimeout, "PendingTimeout");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();

    /* Reply to the second one, let the first one to time out */
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_response_empty_ok, test);

    grilio_request_set_blocking(t->req1, TRUE);
    grilio_request_set_timeout(t->req1, -1);
    grilio_channel_send_request(test->io, t->req1, RIL_REQUEST_TEST_1);
    grilio_channel_send_request_full(test->io, t->req2, RIL_REQUEST_TEST_2,
        test_pending_timeout_req2_completed, NULL, t);

    /* First request expires in 10 ms and the second one is sent */
    grilio_channel_set_pending_timeout(test->io, 1);
    grilio_channel_set_pending_timeout(test->io, 10);
    /* Resistance to invalid parameters */
    grilio_channel_set_pending_timeout(NULL, 0);
    grilio_channel_set_pending_timeout(test->io, 0);

    /* Run the test */
    g_main_loop_run(test->loop);

    /* The first request remains in the SENT state */
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    test_free(test);
}


/*==========================================================================*
 * Drop
 *==========================================================================*/

typedef struct test_drop_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
    gboolean req1_done;
    gboolean req2_done;
    gboolean req3_done;
} TestDrop;

static
void
test_drop_req_done(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    gboolean* done = user_data;
    (*done) = TRUE;
}

static
void
test_drop_req3_done(
    GRilIoChannel* channel,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestDrop* t = user_data;
    g_assert(status == GRILIO_STATUS_OK);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_DONE);
    t->req3_done = TRUE;
    g_main_loop_quit(t->test.loop);
}

static
gboolean
test_drop_req1_expired(
    gpointer user_data)
{
    TestDrop* t = user_data;
    guint id1 = grilio_request_id(t->req1);
    g_assert(!t->req2_done);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_SENT);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_QUEUED);
    g_assert(grilio_channel_get_request(t->test.io,
        grilio_request_id(t->req3)) == t->req3);
    grilio_channel_drop_request(t->test.io, id1);
    grilio_channel_drop_request(t->test.io, id1); /* And again */
    return G_SOURCE_REMOVE;
}

static
void
test_drop_connected(
    GRilIoChannel* io,
    void* user_data)
{
    TestDrop* t = user_data;
    g_timeout_add(10, test_drop_req1_expired, t);
}

static
void
test_drop(
    void)
{
    TestDrop* t = test_new(TestDrop, "Drop");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_2,
        test_response_empty_ok, t);
    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST_3,
        test_response_empty_ok, t);

    /* There won't be any reply to req1 */
    grilio_request_set_timeout(t->req1, 0);
    grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST_1,
        test_drop_req_done, NULL, &t->req1_done);
    /* But req2 and req3 will be allowed to run after we drop req1 */
    grilio_request_set_blocking(t->req2, TRUE);
    grilio_channel_send_request_full(test->io, t->req2, RIL_REQUEST_TEST_2,
        test_drop_req_done, NULL, &t->req2_done);
    grilio_request_set_blocking(t->req3, TRUE);
    grilio_channel_send_request_full(test->io, t->req3, RIL_REQUEST_TEST_3,
        test_drop_req3_done, NULL, t);

    /* NULL is ignored */
    grilio_channel_drop_request(NULL, 0);

    /* Run the test */
    grilio_channel_add_connected_handler(test->io, test_drop_connected, t);
    g_main_loop_run(test->loop);

    g_assert(!t->req1_done);
    g_assert(t->req2_done);
    g_assert(t->req3_done);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_DONE);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    test_free(test);
}

/*==========================================================================*
 * Cancel1
 *==========================================================================*/

typedef struct test_cancel1_data {
    Test test;
    GRilIoRequest* req1;
    GRilIoRequest* req2;
    GRilIoRequest* req3;
} TestCancel1;

static
void
test_cancel1_req2_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestCancel1* t = user_data;
    GDEBUG("Request 2 completion status %d", status);
    g_assert(status == GRILIO_STATUS_CANCELLED);
    /* This one has already been cancelled */
    g_assert(!grilio_channel_cancel_request(t->test.io,
        grilio_request_id(t->req2), TRUE));
    /* Cancel req3 from the req2 completion callback */
    g_assert(grilio_channel_cancel_request(t->test.io,
        grilio_request_id(t->req3), TRUE));
    g_main_loop_quit(t->test.loop);
}

static
void
test_cancel1_req1_completed(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    TestCancel1* t = user_data;
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    /* Cancel everything */
    grilio_channel_cancel_all(t->test.io, TRUE);
    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_CANCELLED);
}

static
void
test_cancel1(
    void)
{
    TestCancel1* t = test_new(TestCancel1, "Cancel1");
    Test* test = &t->test;

    t->req1 = grilio_request_new();
    t->req2 = grilio_request_new();
    t->req3 = grilio_request_new();

    grilio_test_server_add_request_func(test->server, RIL_REQUEST_TEST,
        test_response_empty_ok, test);

    grilio_channel_send_request_full(test->io, t->req1, RIL_REQUEST_TEST,
        test_cancel1_req1_completed, NULL, t);
    grilio_channel_send_request_full(test->io, t->req2, RIL_REQUEST_TEST,
        test_cancel1_req2_completed, NULL, t);
    grilio_channel_send_request(test->io, t->req3, RIL_REQUEST_TEST);

    /* Run the test */
    g_main_loop_run(test->loop);

    g_assert(grilio_request_status(t->req1) == GRILIO_REQUEST_DONE);
    g_assert(grilio_request_status(t->req2) == GRILIO_REQUEST_CANCELLED);
    g_assert(grilio_request_status(t->req3) == GRILIO_REQUEST_CANCELLED);
    grilio_request_unref(t->req1);
    grilio_request_unref(t->req2);
    grilio_request_unref(t->req3);
    test_free(test);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/io/"

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "Connected", test_connected);
    g_test_add_func(TEST_PREFIX "Basic", test_basic);
    g_test_add_func(TEST_PREFIX "Queue", test_queue);
    g_test_add_func(TEST_PREFIX "Transaction1", test_transaction1);
    g_test_add_func(TEST_PREFIX "Transaction2", test_transaction2);
    g_test_add_func(TEST_PREFIX "WriteError1", test_write_error1);
    g_test_add_func(TEST_PREFIX "WriteError2", test_write_error2);
    g_test_add_func(TEST_PREFIX "WriteError3", test_write_error3);
    g_test_add_func(TEST_PREFIX "Disconnect", test_disconnect);
    g_test_add_func(TEST_PREFIX "ShortPacket", test_short_packet);
    g_test_add_func(TEST_PREFIX "Logger", test_logger);
    g_test_add_func(TEST_PREFIX "Handlers", test_handlers);
    g_test_add_func(TEST_PREFIX "EarlyResp", test_early_resp);
    g_test_add_func(TEST_PREFIX "Retry1", test_retry1);
    g_test_add_func(TEST_PREFIX "Retry2", test_retry2);
    g_test_add_func(TEST_PREFIX "Retry3", test_retry3);
    g_test_add_func(TEST_PREFIX "Timeout1", test_timeout1);
    g_test_add_func(TEST_PREFIX "Timeout2", test_timeout2);
    g_test_add_func(TEST_PREFIX "Serialize1", test_serialize1);
    g_test_add_func(TEST_PREFIX "Serialize2", test_serialize2);
    g_test_add_func(TEST_PREFIX "Serialize3", test_serialize3);
    g_test_add_func(TEST_PREFIX "Block1", test_block1);
    g_test_add_func(TEST_PREFIX "Block2", test_block2);
    g_test_add_func(TEST_PREFIX "BlockTimeout", test_block_timeout);
    g_test_add_func(TEST_PREFIX "PendingTimeout", test_pending_timeout);
    g_test_add_func(TEST_PREFIX "Drop", test_drop);
    g_test_add_func(TEST_PREFIX "Cancel1", test_cancel1);
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
