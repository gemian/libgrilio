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

#define RIL_REQUEST_TEST 51
#define RIL_UNSOL_RIL_CONNECTED 1034
#define RIL_E_GENERIC_FAILURE 2

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
    grilio_channel_set_timeout(NULL, 0);
    grilio_channel_cancel_all(NULL, FALSE);
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
    TestQueue* queue = user_data;
    GDEBUG("Request destroyed");
    queue->destroy_count++;
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
    TestQueue* queue = user_data;
    if (status == GRILIO_STATUS_CANCELLED) { 
        queue->cancel_count++;
        GDEBUG("%d request(s) cancelled", queue->cancel_count);
    } else if (status == GRILIO_STATUS_OK) {
        queue->success_count++;
        GDEBUG("%d request(s) cancelled", queue->cancel_count);
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
    TestQueue* queue = user_data;
    GDEBUG("Last response status %d", status);
    g_assert(status == GRILIO_STATUS_OK);

    /* 4 events should be cancelled, first one succeed,
     * this one doesn't count */
    if (queue->cancel_count == 4 &&
        queue->success_count == 1 &&
        queue->destroy_count == 1) {
        g_main_loop_quit(queue->test.loop);
    }
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
    TestQueue* queue = user_data;
    GDEBUG("First response status %d", status);
    if (status == GRILIO_STATUS_OK) {
        queue->success_count++;
        grilio_queue_cancel_all(queue->queue[1], TRUE);
        grilio_queue_cancel_request(queue->queue[0], queue->cancel_id, TRUE);

        g_assert(!queue->last_id);
        queue->last_id = test_basic_request(&queue->test,
            test_queue_last_response);

        /* This one stops the event loop */
        test_basic_response_ok(queue->test.server, "TEST", queue->last_id);

        /* This will deallocate the queue, cancelling all the requests in
         * the process. Callbacks won't be notified. Extra ref just improves
         * the code coverage. */
        grilio_queue_ref(queue->queue[2]);
        grilio_queue_unref(queue->queue[2]);
        grilio_queue_unref(queue->queue[2]);
        queue->queue[2] = NULL;
    }
}

static
void
test_queue_start(
    GRilIoChannel* channel,
    void* user_data)
{
    TestQueue* queue = user_data;
    guint id;

    /* NULL resistance */
    g_assert(!grilio_queue_send_request_full(NULL, NULL, 0, NULL, NULL, NULL));

    /* This entire queue will be cancelled */
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);

    /* This one will invoke test_queue_destroy when canceled which will
     * increment destroy_count */
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response,
        test_queue_destroy_request, queue);

    /* Expected failure to cancel a request that's not in a queue */
    id = grilio_channel_send_request_full(queue->test.io, NULL,
        RIL_REQUEST_TEST, test_queue_no_response, NULL, NULL);
    g_assert(id);
    g_assert(!grilio_queue_cancel_request(queue->queue[0], id, FALSE));
    g_assert(grilio_channel_cancel_request(queue->test.io, id, FALSE));

    /* Cancel request without callback */
    grilio_queue_cancel_request(queue->queue[1],
        grilio_queue_send_request(queue->queue[1], NULL,
            RIL_REQUEST_TEST), FALSE);

    /* This one will be cancelled implicitely, when queue will get
     * deallocated. Callbacks won't be notified. */
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);
    grilio_queue_send_request(queue->queue[2], NULL,
        RIL_REQUEST_TEST);

    /* This one will succeed */
    test_basic_response_ok(queue->test.server, "QUEUE_TEST",
        grilio_queue_send_request_full(queue->queue[0], NULL,
            RIL_REQUEST_TEST, test_queue_first_response,
            NULL, queue));

    /* This one from queue 0 will be cancelled too */
    queue->cancel_id = grilio_queue_send_request_full(queue->queue[0], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, queue);
    test_basic_response_ok(queue->test.server, "CANCEL", queue->cancel_id);
}

static
void
test_queue(
    void)
{
    TestQueue* queue = test_new(TestQueue, "Queue");
    Test* test = &queue->test;

    queue->queue[0] = grilio_queue_new(test->io);
    queue->queue[1] = grilio_queue_new(test->io);
    queue->queue[2] = grilio_queue_new(test->io);

    /* There are no requests with zero id */
    g_assert(!grilio_queue_cancel_request(queue->queue[0], 0, FALSE));

    /* Test NULL resistance */
    g_assert(!grilio_queue_ref(NULL));
    g_assert(!grilio_queue_new(NULL));
    grilio_queue_cancel_request(NULL, 0, FALSE);
    grilio_queue_cancel_all(NULL, FALSE);

    /* First wait until we get connected to the test server */
    queue->connected_id = grilio_channel_add_connected_handler(test->io,
        test_queue_start, queue);

    /* Run the test */
    g_main_loop_run(test->loop);

    g_assert(queue->last_id);
    grilio_queue_cancel_all(queue->queue[0], FALSE);
    grilio_queue_cancel_all(queue->queue[1], FALSE);
    grilio_queue_unref(queue->queue[0]);
    grilio_queue_unref(queue->queue[1]);
    g_assert(!queue->queue[2]); /* This one should already be NULL */
    grilio_queue_unref(queue->queue[2]);
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
    g_assert(code == TEST_HANDLERS_INC_EVENT);
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
    int i;

    /* Prepare the test */
    for (i=0; i<TEST_HANDLERS_COUNT; i++) {
        h->id1[i] = grilio_channel_add_unsol_event_handler(test->io,
            test_handlers_inc, TEST_HANDLERS_INC_EVENT, &h->count1);
        h->id2[i] = grilio_channel_add_unsol_event_handler(test->io,
            test_handlers_inc, TEST_HANDLERS_INC_EVENT, &h->count2);
    }
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
    for (i=0; i<TEST_HANDLERS_COUNT; i++) {
        g_assert(!h->id2[i]);
    }

    /* Clean up */
    grilio_channel_remove_handlers(test->io, h->id1, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handlers(test->io, h->id2, TEST_HANDLERS_COUNT);
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
        retry->req2->req_id, RIL_E_GENERIC_FAILURE);
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
        retry->req2->req_id, RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        retry->req3->req_id, RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        retry->req4->req_id, RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        retry->req5->req_id, RIL_E_GENERIC_FAILURE);
    grilio_test_server_add_response(test->server, NULL,
        retry->req6->req_id, RIL_E_GENERIC_FAILURE);
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
    g_assert(retry->req2_status == RIL_E_GENERIC_FAILURE);
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
gboolean
test_retry3_check_req(
    GRilIoChannel* io,
    GRilIoRequest* req,
    guint dummy_id)
{
    const guint id = grilio_request_id(req);
    if (grilio_channel_retry_request(io, dummy_id)) {
        GDEBUG("Retry with dummy id unexpectedly succeeded");
    } else if (!grilio_channel_retry_request(io, id)) {
        GDEBUG("Failed to retry");
    } else if (grilio_channel_retry_request(io, id)) {
        GDEBUG("Second retry unexpectedly succeeded");
    } else if (grilio_channel_retry_request(io, dummy_id)) {
        GDEBUG("Retry with dummy id unexpectedly succeeded");
    } else if (!grilio_channel_cancel_request(io, id, TRUE)) {
        GDEBUG("Failed to cancel");
    } else {
        return TRUE;
    }
    return FALSE;
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
        if (test_retry3_check_req(io, retry->req1, retry->dummy_id) &&
            test_retry3_check_req(io, retry->req2, retry->dummy_id)) {
            /* All good, done with the test */
            g_main_loop_quit(test->loop);
        }
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
 * Timeout
 *==========================================================================*/

typedef struct test_timeout_data {
    Test test;
    int timeout_count;
    guint req_id;
    guint timer_id;
} TestTimeout;

static
gboolean
test_timeout_done(
    gpointer user_data)
{
    Test* test = user_data;
    TestTimeout* timeout = G_CAST(test, TestTimeout, test);
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
test_timeout_response(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestTimeout* timeout = G_CAST(test, TestTimeout, test);
    GDEBUG("Completion status %d", status);
    if (status == GRILIO_STATUS_TIMEOUT) {
        timeout->timeout_count++;
        if (!timeout->timer_id) {
            timeout->timer_id = g_timeout_add(200, test_timeout_done, test);
        }
    }
}

static
void
test_timeout_submit_requests(
    Test* test,
    GRilIoChannelResponseFunc fn)
{
    TestTimeout* timeout = G_CAST(test, TestTimeout, test);
    GRilIoRequest* req1 = grilio_request_new();
    GRilIoRequest* req2 = grilio_request_new();
    grilio_channel_set_timeout(test->io, 10);
    grilio_request_set_timeout(req2, INT_MAX);

    grilio_channel_send_request_full(test->io, req1,
        RIL_REQUEST_TEST, fn, NULL, test);
    timeout->req_id = grilio_channel_send_request_full(test->io, req2,
        RIL_REQUEST_TEST, test_timeout_response, NULL, test);
    grilio_request_unref(req1);
    grilio_request_unref(req2);
}

static
void
test_timeout_start(
    GRilIoChannel* io,
    int status,
    const void* data,
    guint len,
    void* user_data)
{
    Test* test = user_data;
    TestTimeout* timeout = G_CAST(test, TestTimeout, test);
    if (status == GRILIO_STATUS_TIMEOUT) {
        GDEBUG("Starting...");
        if (grilio_channel_cancel_request(test->io, timeout->req_id, FALSE)) {
            test_timeout_submit_requests(test, test_timeout_response);
        }
    }
}

static
void
test_timeout(
    void)
{
    TestTimeout* timeout = test_new(TestTimeout, "Timeout");
    Test* test = &timeout->test;
    test_timeout_submit_requests(test, test_timeout_start);
    g_assert(!timeout->timer_id);
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
    g_test_add_func(TEST_PREFIX "Timeout", test_timeout);
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
