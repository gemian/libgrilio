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
 *   3. Neither the name of the Jolla Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
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

#include "grilio_channel.h"
#include "grilio_request.h"
#include "grilio_parser.h"
#include "grilio_queue.h"

#include "grilio_test_server.h"
#include "grilio_p.h"

#include <gutil_log.h>
#include <gutil_macros.h>

#define RET_OK       (0)
#define RET_ERR      (1)
#define RET_TIMEOUT  (2)

#define TEST_TIMEOUT (10) /* seconds */

#define RIL_REQUEST_TEST 51
#define RIL_UNSOL_RIL_CONNECTED 1034
#define RIL_E_GENERIC_FAILURE 2

typedef struct test_desc TestDesc;
typedef struct test {
    const TestDesc* desc;
    GMainLoop* loop;
    GRilIoTestServer* server;
    GRilIoChannel* io;
    guint timeout_id;
    guint log;
    int ret;
} Test;

struct test_desc {
    const char* name;
    gsize size;
    gboolean (*init)(Test* test);
    int (*check)(Test* test);
    void (*destroy)(Test* test);
};

/*==========================================================================*
 * Connected
 *==========================================================================*/

typedef struct test_connected {
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
    TestConnected* test = G_CAST(user_data, TestConnected, test);
    GASSERT(code == RIL_UNSOL_RIL_CONNECTED);
    if (code == RIL_UNSOL_RIL_CONNECTED) {
        GRilIoParser parser;
        int count = 0;
        guint32 version = 0;
        grilio_parser_init(&parser, data, len);
        if (grilio_parser_get_int32(&parser, &count) &&
            grilio_parser_get_uint32(&parser, &version) &&
            grilio_parser_at_end(&parser)) {
            GDEBUG("RIL version %u", version);
            if (count == 1 && version == GRILIO_RIL_VERSION) {
                grilio_channel_remove_handler(test->test.io, test->event_id);
                test->event_id = 0;
                test->event_count++;
            }
        }
    }
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
    TestConnected* test = G_CAST(user_data, TestConnected, test);
    grilio_channel_remove_handler(test->test.io, test->connected_id);
    test->connected_id = 0;
    test->event_count++;
    if (test->event_count == 2) {
        g_main_loop_quit(test->test.loop);
    }
}

static
gboolean
test_connected_init(
    Test* test)
{
    TestConnected* tc = G_CAST(test, TestConnected, test);
    tc->event_id = grilio_channel_add_unsol_event_handler(test->io,
            test_connected_event, RIL_UNSOL_RIL_CONNECTED, test);
    tc->connected_id = grilio_channel_add_connected_handler(test->io,
            test_connected_callback, test);
    return tc->event_id != 0 && tc->connected_id;
}

static
int
test_connected_check(
    Test* test)
{
    TestConnected* tc = G_CAST(test, TestConnected, test);
    if (tc->event_count == 2 && !tc->connected_id && !tc->event_id) {
        return RET_OK;
    } else {
        return RET_ERR;
    }
}

static
void
test_connected_destroy(
    Test* test)
{
    TestConnected* connected = G_CAST(test, TestConnected, test);
    grilio_channel_remove_handler(test->io, connected->event_id);
}

/*==========================================================================*
 * Basic
 *==========================================================================*/

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
    if (status == 0) {
        GRilIoParser parser;
        char* info;
        grilio_parser_init(&parser, data, len);
        info = grilio_parser_get_utf8(&parser);
        if (info) {
            GDEBUG("Baseband version: %s", info);
            g_free(info);
            if (grilio_parser_at_end(&parser)) {
                grilio_parser_init(&parser, data, len);
                if (grilio_parser_skip_string(&parser) &&
                    grilio_parser_at_end(&parser)) {
                    test->ret = RET_OK;
                }
            }
        }
    }
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
gboolean
test_basic_init(
    Test* test)
{
    /* Test send/cancel before we are connected to the server. */
    guint id = grilio_channel_send_request(test->io, NULL, 0);
    if (grilio_channel_cancel_request(test->io, id, FALSE)) {
        grilio_test_server_set_chunk(test->server, 5);
        return test_basic_response_ok(test->server, "UNIT_TEST",
            test_basic_request(test, test_basic_response));
    }
    return FALSE;
}

/*==========================================================================*
 * Queue
 *==========================================================================*/

typedef struct test_queue {
    Test test;
    int cancel_count;
    int success_count;
    int destroy_count;
    GRilIoQueue* queue[3];
    guint cancel_id;
} TestQueue;

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
    if (status == GRILIO_STATUS_OK) {
        /* 4 events should be cancelled, first one succeed,
         * this one doesn't count */
        if (queue->cancel_count == 4 &&
            queue->success_count == 1 &&
            queue->destroy_count == 1) {
            queue->test.ret = RET_OK;
        }
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
        /* This one will stop the event loop */
        test_basic_response_ok(queue->test.server, "TEST",
            test_basic_request(&queue->test, test_queue_last_response));

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
gboolean
test_queue_init(
    Test* test)
{
    TestQueue* queue = G_CAST(test, TestQueue, test);
    queue->queue[0] = grilio_queue_new(test->io);
    queue->queue[1] = grilio_queue_new(test->io);
    queue->queue[2] = grilio_queue_new(test->io);

    /* This entire queue will be cancelled */
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);

    /* This one will invoke test_queue_destroy when cancedl which will
     * increment destroy_count */
    grilio_queue_send_request_full(queue->queue[1], NULL,
        RIL_REQUEST_TEST, test_queue_response,
        test_queue_destroy_request, test);

    /* Cancel request without callback */
    grilio_queue_cancel_request(queue->queue[1],
        grilio_queue_send_request(queue->queue[1], NULL,
            RIL_REQUEST_TEST), FALSE);

    /* This one will be cancelled impplicitely, when queue will get
     * deallocated. Callbacks won't be notified. */
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);
    grilio_queue_send_request_full(queue->queue[2], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);
    grilio_queue_send_request(queue->queue[2], NULL,
        RIL_REQUEST_TEST);

    /* This one will succeed */
    test_basic_response_ok(test->server, "QUEUE_TEST",
        grilio_queue_send_request_full(queue->queue[0], NULL,
            RIL_REQUEST_TEST, test_queue_first_response,
            NULL, test));

    /* This one from queue 0 will be cancelled too */
    queue->cancel_id = grilio_queue_send_request_full(queue->queue[0], NULL,
        RIL_REQUEST_TEST, test_queue_response, NULL, test);
    test_basic_response_ok(test->server, "CANCEL", queue->cancel_id);
    return TRUE;
}

static
void
test_queue_destroy(
    Test* test)
{
    TestQueue* queue = G_CAST(test, TestQueue, test);
    grilio_queue_cancel_all(queue->queue[0], FALSE);
    grilio_queue_cancel_all(queue->queue[1], FALSE);
    grilio_queue_unref(queue->queue[0]);
    grilio_queue_unref(queue->queue[1]);
    grilio_queue_unref(queue->queue[2]); /* Should already be NULL */
}

/*==========================================================================*
 * WriteError
 *==========================================================================*/

static
void
test_write_error(
    GRilIoChannel* io,
    const GError* error,
    void* user_data)
{
    Test* test = user_data;
    GDEBUG("%s", GERRMSG(error));
    test->ret = RET_OK;
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
test_write_error1(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error, test);
    grilio_channel_send_request(io, NULL, RIL_REQUEST_TEST);
}

static
void
test_write_error2(
    GRilIoChannel* io,
    void* user_data)
{
    guint id;
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error, test);
    id = grilio_channel_send_request(io, NULL, RIL_REQUEST_TEST);
    /* The first cancel should succeed, the second one fail */
    if (!grilio_channel_cancel_request(io, id, TRUE) ||
        grilio_channel_cancel_request(io, id, TRUE) ||
        /* There's no requests with zero id */
        grilio_channel_cancel_request(io, 0, TRUE)){
        test->ret = RET_ERR;
    }
}

static
void
test_write_error3(
    GRilIoChannel* io,
    void* user_data)
{
    guint id;
    Test* test = user_data;
    grilio_test_server_shutdown(test->server);
    /* This should result in test_write_error getting invoked */
    grilio_channel_add_error_handler(test->io, test_write_error, test);
    id = grilio_channel_send_request_full(io, NULL,
        RIL_REQUEST_TEST, test_write_completion, NULL, test);
    /* The first cancel should succeed, the second one fail */
    if (!grilio_channel_cancel_request(io, id, TRUE) ||
        grilio_channel_cancel_request(io, id, TRUE) ||
        /* INT_MAX is a non-existent id */
        grilio_channel_cancel_request(io, INT_MAX, TRUE)) {
        test->ret = RET_ERR;
    }
}

static
gboolean
test_write_error1_init(
    Test* test)
{
    grilio_channel_add_connected_handler(test->io, test_write_error1, test);
    /* grilio_channel_new_socket("/tmp" must fail */
    return !grilio_channel_new_socket("/tmp", NULL);
}

static
gboolean
test_write_error2_init(
    Test* test)
{
    grilio_channel_add_connected_handler(test->io, test_write_error2, test);
    /* grilio_channel_new_socket("/tmp" must fail */
    return !grilio_channel_new_socket("/tmp", NULL);
}

static
gboolean
test_write_error3_init(
    Test* test)
{
    grilio_channel_add_connected_handler(test->io, test_write_error3, test);
    /* grilio_channel_new_socket("/tmp" must fail */
    return !grilio_channel_new_socket("/tmp", NULL);
}

/*==========================================================================*
 * EOF
 *==========================================================================*/

static
void
test_eof_handler(
    GRilIoChannel* io,
    void* user_data)
{
    Test* test = user_data;
    test->ret = RET_OK;
    g_main_loop_quit(test->loop);
}

static
gboolean
test_eof_init(
    Test* test)
{
    grilio_channel_add_disconnected_handler(test->io, test_eof_handler, test);
    grilio_test_server_shutdown(test->server);
    return TRUE;
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
    test->ret = RET_OK;
    g_main_loop_quit(test->loop);
}

static
gboolean
test_short_packet_init(
    Test* test)
{
    static char data[2] = {0xff, 0xff};
    guint32 pktlen = GINT32_TO_BE(sizeof(data));
    grilio_channel_add_error_handler(test->io, test_short_packet_handler, test);
    grilio_test_server_add_data(test->server, &pktlen, sizeof(pktlen));
    grilio_test_server_add_data(test->server, data, sizeof(data));
    /* These two do nothing (but improve branch coverage): */
    grilio_channel_add_error_handler(NULL, test_short_packet_handler, NULL);
    grilio_channel_add_error_handler(test->io, NULL, NULL);
    return TRUE;
}

/*==========================================================================*
 * Logger
 *==========================================================================*/

typedef struct test_logger {
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
        log->test.ret = RET_OK;
        g_main_loop_quit(log->test.loop);
    }
}

static
gboolean
test_logger_init(
    Test* test)
{
    TestLogger* log = G_CAST(test, TestLogger, test);
    guint id[3];

    /* Remove default logger and re-add it with GLOG_LEVEL_ALWAYS, mainly
     * to improve code coverage. Remove it twice to make sure that invalid
     * logger ids are handled properly, i.e. ignored. */
    grilio_channel_remove_logger(test->io, test->log);
    grilio_channel_remove_logger(test->io, test->log);
    test->log = grilio_channel_add_default_logger(test->io, GLOG_LEVEL_ALWAYS);
    log->test_log = grilio_channel_add_logger(test->io, test_logger_cb, log);

    id[0] = test_basic_request(test, test_logger_response);
    id[1] = test_basic_request(test, test_logger_response);
    id[2] = test_basic_request(test, test_logger_response);
    grilio_channel_cancel_request(test->io, id[0], TRUE);
    grilio_channel_cancel_request(test->io, id[1], FALSE);
    return id[0] && id[1] && id[2] &&
        test_basic_response_ok(test->server, "LOGTEST", id[2]);
}

static
void
test_logger_destroy(
    Test* test)
{
    TestLogger* log = G_CAST(test, TestLogger, test);
    grilio_channel_remove_logger(test->io, log->test_log);
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

typedef struct test_handlers {
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
    GASSERT(code == TEST_HANDLERS_REMOVE_EVENT);
    if (code == TEST_HANDLERS_REMOVE_EVENT) {
        TestHandlers* h = user_data;
        Test* test = &h->test;
        int i;

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
    GASSERT(code == TEST_HANDLERS_INC_EVENT);
    if (code == TEST_HANDLERS_INC_EVENT) {
        (*count)++;
        GDEBUG("Event %u data %p value %d", code, count, *count);
    }
}

static
gboolean
test_handlers_init(
    Test* test)
{
    TestHandlers* h = G_CAST(test, TestHandlers, test);
    int i;
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
    return TRUE;
}

static
int
test_handlers_check(
    Test* test)
{
    TestHandlers* h = G_CAST(test, TestHandlers, test);
    if (h->count1 == 2*TEST_HANDLERS_COUNT*TEST_HANDLERS_INC_EVENTS_COUNT &&
        h->count2 == TEST_HANDLERS_COUNT*TEST_HANDLERS_INC_EVENTS_COUNT) {
        int i;
        for (i=0; i<TEST_HANDLERS_COUNT; i++) {
            GASSERT(!h->id2[i]);
            if (h->id2[i]) {
                return RET_ERR;
            }
        }
        return RET_OK;
    }
    return RET_ERR;
}

static
void
test_handlers_destroy(
    Test* test)
{
    TestHandlers* h = G_CAST(test, TestHandlers, test);
    grilio_channel_remove_handlers(test->io, h->id1, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handlers(test->io, h->id2, TEST_HANDLERS_COUNT);
    grilio_channel_remove_handler(test->io, h->next_event_id);
    /* These do nothing: */
    grilio_channel_remove_handler(test->io, 0);
    grilio_channel_remove_handler(NULL, 0);
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

typedef struct test_retry1 {
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
gboolean
test_retry1_init(
    Test* test)
{
    TestRetry1* retry = G_CAST(test, TestRetry1, test);

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

    GASSERT(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry1_start, test);
    return TRUE;
}

static
int
test_retry1_check(
    Test* test)
{
    TestRetry1* retry = G_CAST(test, TestRetry1, test);
    if (grilio_request_status(retry->req4) != GRILIO_REQUEST_RETRY) {
        GDEBUG("Unexpected request 4 status %d",
            grilio_request_status(retry->req4));
    } else if (grilio_request_status(retry->req5) != GRILIO_REQUEST_RETRY) {
        GDEBUG("Unexpected request 5 status %d",
            grilio_request_status(retry->req5));
    } else if (grilio_request_status(retry->req6) != GRILIO_REQUEST_RETRY) {
        GDEBUG("Unexpected request 6 status %d",
            grilio_request_status(retry->req6));
    } else {
        grilio_channel_cancel_request(test->io,
            grilio_request_id(retry->req5), TRUE);
        grilio_channel_cancel_request(test->io,
            grilio_request_id(retry->req4), TRUE);
        grilio_channel_cancel_all(test->io, TRUE);
        if (grilio_request_status(retry->req4) !=GRILIO_REQUEST_CANCELLED) {
            GDEBUG("Unexpected request 4 status %d after cancel",
                grilio_request_status(retry->req4));
        } else if (grilio_request_status(retry->req5) !=
                   GRILIO_REQUEST_CANCELLED) {
            GDEBUG("Unexpected request 5 status %d after cancel",
                grilio_request_status(retry->req5));
        } else if (retry->req2_status != RIL_E_GENERIC_FAILURE) {
            GDEBUG("Unexpected request 2 completion status %d",
                retry->req2_status);
        } else if (retry->req3_completed != 1) {
            GDEBUG("Unexpected request 3 completion count %d",
                retry->req3_completed);
        } else if (retry->req4_completed != 1) {
            GDEBUG("Unexpected request 4 completion count %d",
                retry->req4_completed);
        } else if (grilio_request_retry_count(retry->req1) != 1) {
            GDEBUG("Unexpected request 1 retry count %d",
                grilio_request_retry_count(retry->req1));
        } else if (grilio_request_retry_count(retry->req2) != 1) {
            GDEBUG("Unexpected request 3 retry count %d",
                grilio_request_retry_count(retry->req2));
        } else if (grilio_request_retry_count(retry->req3) != 1) {
            GDEBUG("Unexpected request 3 retry count %d",
                grilio_request_retry_count(retry->req3));
        } else if (grilio_request_retry_count(retry->req4) != 0) {
            GDEBUG("Unexpected request 4 retry count %d",
                grilio_request_retry_count(retry->req4));
        } else {
            return RET_OK;
        }
    }
    return RET_ERR;
}

static
void
test_retry1_destroy(
    Test* test)
{
    TestRetry1* retry = G_CAST(test, TestRetry1, test);
    grilio_request_unref(retry->req1);
    grilio_request_unref(retry->req2);
    grilio_request_unref(retry->req3);
    grilio_request_unref(retry->req4);
    grilio_request_unref(retry->req5);
    grilio_request_unref(retry->req6);
}

/*==========================================================================*
 * Retry2
 *
 * Makes sure that request ids are incremented on each retry. Two requests
 * are rejected, the third one times out.
 *==========================================================================*/

typedef struct test_retry2 {
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
            GASSERT(retry->req_id == id);
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
gboolean
test_retry2_init(
    Test* test)
{
    TestRetry2* retry = G_CAST(test, TestRetry2, test);

    retry->log_id = grilio_channel_add_logger(test->io, test_retry2_log, test);
    retry->req = grilio_request_new();
    grilio_request_set_retry(retry->req, 10, 2);

    GASSERT(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry2_start, test);
    return TRUE;
}

static
int
test_retry2_check(
    Test* test)
{
    TestRetry2* retry = G_CAST(test, TestRetry2, test);
    if (grilio_request_retry_count(retry->req) != 2) {
        GDEBUG("Unexpected request retry count %d",
            grilio_request_retry_count(retry->req));
    } else {
        return RET_OK;
    }
    return RET_ERR;
}

static
void
test_retry2_destroy(
    Test* test)
{
    TestRetry2* retry = G_CAST(test, TestRetry2, test);
    guint id = grilio_request_id(retry->req);
    grilio_channel_cancel_request(test->io, id, FALSE);
    grilio_request_unref(retry->req);
    grilio_channel_remove_logger(test->io, retry->log_id);
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

typedef struct test_retry3 {
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
gboolean
test_retry3_init(
    Test* test)
{
    TestRetry3* retry = G_CAST(test, TestRetry3, test);

    retry->req1 = grilio_request_new();
    retry->req2 = grilio_request_new();
    grilio_request_set_timeout(retry->req1, INT_MAX);
    grilio_request_set_timeout(retry->req2, INT_MAX);
    grilio_request_set_retry(retry->req1, INT_MAX, 1);
    grilio_request_set_retry(retry->req2, INT_MAX-1, 1);

    GASSERT(!test->io->connected);
    grilio_channel_add_connected_handler(test->io, test_retry3_start, test);
    return TRUE;
}

static
int
test_retry3_check(
    Test* test)
{
    TestRetry3* retry = G_CAST(test, TestRetry3, test);
    if (grilio_request_retry_count(retry->req1) != 1) {
        GDEBUG("Unexpected request 1 retry count %d",
            grilio_request_retry_count(retry->req1));
    } else if (grilio_request_retry_count(retry->req2) != 1) {
        GDEBUG("Unexpected request 2 retry count %d",
            grilio_request_retry_count(retry->req2));
    } else if (retry->status1 != GRILIO_STATUS_CANCELLED) {
        GDEBUG("Unexpected completion status %d for req 1", retry->status1);
    } else if (retry->status2 != GRILIO_STATUS_CANCELLED) {
        GDEBUG("Unexpected completion status %d for req 2", retry->status2);
    } else {
        return RET_OK;
    }
    return RET_ERR;
}

static
void
test_retry3_destroy(
    Test* test)
{
    TestRetry3* retry = G_CAST(test, TestRetry3, test);
    const guint id1 = grilio_request_id(retry->req1);
    const guint id2 = grilio_request_id(retry->req2);
    grilio_channel_cancel_request(test->io, id1, FALSE);
    grilio_channel_cancel_request(test->io, id2, FALSE);
    grilio_request_unref(retry->req1);
    grilio_request_unref(retry->req2);
}

/*==========================================================================*
 * Timeout
 *==========================================================================*/

typedef struct test_timeout {
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
        test->ret = RET_OK;
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
gboolean
test_timeout_init(
    Test* test)
{
    test_timeout_submit_requests(test, test_timeout_start);
    return TRUE;
}

static
void
test_timeout_destroy(
    Test* test)
{
    TestTimeout* timeout = G_CAST(test, TestTimeout, test);
    if (timeout->timer_id) g_source_remove(timeout->timer_id);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static const TestDesc all_tests[] = {
    {
        "Connected",
        sizeof(TestConnected),
        test_connected_init,
        test_connected_check,
        test_connected_destroy
    },{
        "Basic",
        sizeof(Test),
        test_basic_init,
        NULL,
        NULL
    },{
        "Queue",
        sizeof(TestQueue),
        test_queue_init,
        NULL,
        test_queue_destroy
    },{
        "WriteError1",
        sizeof(Test),
        test_write_error1_init,
        NULL,
        NULL
    },{
        "WriteError2",
        sizeof(Test),
        test_write_error2_init,
        NULL,
        NULL
     },{
        "WriteError3",
        sizeof(Test),
        test_write_error3_init,
        NULL,
        NULL
   },{
        "EOF",
        sizeof(Test),
        test_eof_init,
        NULL,
        NULL
    },{
        "ShortPacket",
        sizeof(Test),
        test_short_packet_init,
        NULL,
        NULL
    },{
        "Logger",
        sizeof(TestLogger),
        test_logger_init,
        NULL,
        test_logger_destroy
    },{
        "Handlers",
        sizeof(TestHandlers),
        test_handlers_init,
        test_handlers_check,
        test_handlers_destroy
    },{
        "Retry1",
        sizeof(TestRetry1),
        test_retry1_init,
        test_retry1_check,
        test_retry1_destroy
    },{
        "Retry2",
        sizeof(TestRetry2),
        test_retry2_init,
        test_retry2_check,
        test_retry2_destroy
    },{
        "Retry3",
        sizeof(TestRetry3),
        test_retry3_init,
        test_retry3_check,
        test_retry3_destroy
    },{
        "Timeout",
        sizeof(TestTimeout),
        test_timeout_init,
        NULL,
        test_timeout_destroy
    }
};

static
gboolean
test_timeout(
    gpointer data)
{
    Test* test = data;
    test->timeout_id = 0;
    test->ret = RET_TIMEOUT;
    g_main_loop_quit(test->loop);
    GERR("%s TIMEOUT", test->desc->name);
    return FALSE;
}

static
int
test_done(
    Test* test,
    gboolean destroy)
{
    int ret = ((test->ret != RET_TIMEOUT && test->desc->check) ?
        test->desc->check(test) : test->ret);
    if (test->desc->destroy) test->desc->destroy(test);
    if (destroy && test->timeout_id) g_source_remove(test->timeout_id);
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
    return ret;
}

static
Test*
test_new(
    const TestDesc* desc,
    gboolean debug)
{
    Test* test = g_malloc(desc->size);
    GRilIoTestServer* server = grilio_test_server_new();
    int fd = grilio_test_server_fd(server);
    memset(test, 0, desc->size);
    test->ret = RET_ERR;
    test->loop = g_main_loop_new(NULL, FALSE);
    test->desc = desc;
    test->server = server;
    test->io = grilio_channel_new_fd(fd, "SUB1", FALSE);
    grilio_channel_set_name(test->io, "TEST");
    grilio_channel_set_name(NULL, NULL); /* This one does nothing */
    test->log = grilio_channel_add_default_logger(test->io, GLOG_LEVEL_VERBOSE);
    if (!debug) {
        test->timeout_id = g_timeout_add_seconds(TEST_TIMEOUT,
            test_timeout, test);
    }
    if (desc->init) {
        if (desc->init(test)) {
            return test;
        } else {
            test_done(test, FALSE);
            return NULL;
        }
    } else {
        return test;
    }
}

static
int
test_run_once(
    const TestDesc* desc,
    gboolean debug)
{
    int ret = RET_ERR;
    Test* test = test_new(desc, debug);
    if (test) {
        g_main_loop_run(test->loop);
        ret = test_done(test, TRUE);
    }
    GINFO("%s: %s", (ret == RET_OK) ? "OK" : "FAILED", desc->name);
    return ret;
}

static
int
test_run(
    const char* name,
    gboolean debug)
{
    int i, ret;
    if (name) {
        const TestDesc* found = NULL;
        for (i=0, ret = RET_ERR; i<G_N_ELEMENTS(all_tests); i++) {
            const TestDesc* test = all_tests + i;
            if (!strcmp(test->name, name)) {
                ret = test_run_once(test, debug);
                found = test;
                break;
            }
        }
        if (!found) GERR("No such test: %s", name);
    } else {
        for (i=0, ret = RET_OK; i<G_N_ELEMENTS(all_tests); i++) {
            int test_status = test_run_once(all_tests + i, debug);
            if (ret == RET_OK && test_status != RET_OK) ret = test_status;
        }
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    gboolean debug = FALSE;
    GError* error = NULL;
    GOptionContext* options;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
          "Disable timeout for debugging", NULL },
        { NULL }
    };

    options = g_option_context_new("[TEST]");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        signal(SIGPIPE, SIG_IGN);
        if (verbose) {
            gutil_log_default.level = GLOG_LEVEL_VERBOSE;
        } else {
            gutil_log_timestamp = FALSE;
            gutil_log_default.level = GLOG_LEVEL_INFO;
            grilio_log.level = GLOG_LEVEL_NONE;
        }
        if (argc < 2) {
            ret = test_run(NULL, debug);
        } else {
            int i;
            for (i=1, ret = RET_OK; i<argc; i++) {
                int test_status =  test_run(argv[i], debug);
                if (ret == RET_OK && test_status != RET_OK) ret = test_status;
            }
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
        ret = RET_ERR;
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
