#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gvs_send_transaction.h"
#include "test.h"

struct scripted_sender {
    enum df_gvs_send_attempt_result results[4];
    size_t result_count;
    size_t calls;
    unsigned attempts[4];
    uint64_t completion_ids[4];
    struct df_gvs_reply_queue_entry last_entry;
};

static struct df_gvs_peer_reply make_send_reply(uint8_t extension,
                                                 uint8_t token) {
    struct df_gvs_peer_reply reply = {
        .target = {0x61, 2, 1, 1, 1, extension},
        .request_data = {token, (uint8_t)(token + 1U)},
        .peer_observed = true,
    };

    return reply;
}

static enum df_gvs_send_attempt_result scripted_send(
    const struct df_gvs_reply_queue_entry *entry, unsigned attempt,
    uint64_t completion_id, void *context) {
    struct scripted_sender *sender = context;
    size_t index = sender->calls;

    sender->last_entry = *entry;
    sender->attempts[index] = attempt;
    sender->completion_ids[index] = completion_id;
    sender->calls++;
    if (index >= sender->result_count) {
        return DF_GVS_SEND_ATTEMPT_PENDING;
    }
    return sender->results[index];
}

static void enqueue_reply(struct df_gvs_reply_queue *queue,
                          uint64_t now_ms, uint8_t extension,
                          uint8_t token) {
    struct df_gvs_peer_reply reply = make_send_reply(extension, token);
    bool coalesced = true;

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_reply_queue_enqueue(queue, &reply, now_ms, &coalesced));
    TEST_ASSERT_INT_EQ(0, coalesced);
}

void test_gvs_send_transaction_completes_immediate_success(void) {
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction;
    struct df_gvs_send_trace trace;
    struct scripted_sender sender = {
        .results = {DF_GVS_SEND_ATTEMPT_SUCCESS},
        .result_count = 1,
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 100));
    enqueue_reply(&queue, 100, 1, 0x10);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 100));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_step(&transaction, &queue, 100,
                                             scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SENDING, trace.events[0].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SUCCESS, trace.events[1].type);
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
    TEST_ASSERT_INT_EQ(1, (int)sender.attempts[0]);
    TEST_ASSERT_INT_EQ(0, (int)df_gvs_reply_queue_count(&queue));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_IDLE, transaction.state);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_SUCCESS,
                       transaction.last_outcome);
    TEST_ASSERT_INT_EQ(0x10, sender.last_entry.reply.request_data[0]);
}

void test_gvs_send_transaction_retries_failure_then_succeeds(void) {
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction;
    struct df_gvs_send_trace trace;
    struct scripted_sender sender = {
        .results = {DF_GVS_SEND_ATTEMPT_PENDING,
                    DF_GVS_SEND_ATTEMPT_SUCCESS},
        .result_count = 2,
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 0));
    enqueue_reply(&queue, 0, 2, 0x20);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 0));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_step(&transaction, &queue, 0,
                                             scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(1, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SENDING, trace.events[0].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_SENDING, transaction.state);
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_complete(
                   &transaction, 1, DF_GVS_SEND_ATTEMPT_FAILURE, 50, &trace));
    TEST_ASSERT_INT_EQ(1, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_RETRY, trace.events[0].type);
    TEST_ASSERT_INT_EQ(0, trace.events[0].timed_out);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_WAIT_RETRY, transaction.state);

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_step(&transaction, &queue, 149,
                                             scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_step(&transaction, &queue, 150,
                                             scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SENDING, trace.events[0].type);
    TEST_ASSERT_INT_EQ(2, (int)trace.events[0].attempt);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SUCCESS, trace.events[1].type);
    TEST_ASSERT_INT_EQ(2, (int)sender.calls);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_SUCCESS,
                       transaction.last_outcome);
}

void test_gvs_send_transaction_distinguishes_failure_and_timeout(void) {
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction;
    struct df_gvs_send_trace trace;
    struct scripted_sender failed = {
        .results = {DF_GVS_SEND_ATTEMPT_FAILURE,
                    DF_GVS_SEND_ATTEMPT_FAILURE,
                    DF_GVS_SEND_ATTEMPT_FAILURE},
        .result_count = 3,
    };
    struct scripted_sender pending = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 0));
    enqueue_reply(&queue, 0, 3, 0x30);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 0,
                                      scripted_send, &failed, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 100,
                                      scripted_send, &failed, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 200,
                                      scripted_send, &failed, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_FAILED, trace.events[1].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_FAILED,
                       transaction.last_outcome);
    TEST_ASSERT_INT_EQ(3, (int)failed.calls);

    enqueue_reply(&queue, 201, 4, 0x40);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 201,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_SENDING, transaction.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 451,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_RETRY, trace.events[0].type);
    TEST_ASSERT_INT_EQ(1, trace.events[0].timed_out);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 551,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 801,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 901,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 1151,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_TIMEOUT, trace.events[0].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_TIMEOUT,
                       transaction.last_outcome);
    TEST_ASSERT_INT_EQ(3, (int)pending.calls);
}

void test_gvs_send_transaction_rejects_late_completion_and_bad_time(void) {
    struct df_gvs_reply_queue queue, queue_before;
    struct df_gvs_send_transaction transaction, before;
    struct df_gvs_send_trace trace;
    struct scripted_sender pending = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 10));
    enqueue_reply(&queue, 10, 5, 0x50);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 10,
                                      scripted_send, &pending, &trace));
    before = transaction;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_send_transaction_complete(
            &transaction, 2, DF_GVS_SEND_ATTEMPT_SUCCESS, 11, &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_send_transaction_complete(
            &transaction, 1, DF_GVS_SEND_ATTEMPT_SUCCESS, 9, &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 260,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_RETRY, trace.events[0].type);
    before = transaction;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_send_transaction_complete(
            &transaction, 1, DF_GVS_SEND_ATTEMPT_SUCCESS, 261, &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 360,
                                      scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SENDING, trace.events[0].type);
    TEST_ASSERT_INT_EQ(2, (int)trace.events[0].attempt);
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_send_transaction_complete(
                   &transaction, 2, DF_GVS_SEND_ATTEMPT_SUCCESS, 361, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_SUCCESS, trace.events[0].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_SUCCESS,
                       transaction.last_outcome);
    before = transaction;
    queue_before = queue;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_send_transaction_step(&transaction, &queue, UINT64_MAX,
                                     scripted_send, &pending, &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));
    TEST_ASSERT_INT_EQ(0, memcmp(&queue_before, &queue, sizeof(queue)));
}

void test_gvs_send_transaction_rejects_completion_from_prior_entry(void) {
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction, before;
    struct df_gvs_send_trace trace;
    struct scripted_sender sender = {0};
    uint64_t first_id;
    uint64_t second_id;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 0));
    enqueue_reply(&queue, 0, 1, 0x10);
    enqueue_reply(&queue, 0, 2, 0x20);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 0,
                                      scripted_send, &sender, &trace));
    first_id = sender.completion_ids[0];
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_complete(
                                      &transaction, first_id,
                                      DF_GVS_SEND_ATTEMPT_SUCCESS, 1,
                                      &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 2,
                                      scripted_send, &sender, &trace));
    second_id = sender.completion_ids[1];
    TEST_ASSERT_INT_EQ(0, first_id == second_id);
    before = transaction;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_send_transaction_complete(
                                               &transaction, first_id,
                                               DF_GVS_SEND_ATTEMPT_SUCCESS,
                                               3, &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_complete(
                                      &transaction, second_id,
                                      DF_GVS_SEND_ATTEMPT_SUCCESS, 3,
                                      &trace));
}

void test_gvs_send_transaction_preserves_state_on_queue_clock_error(void) {
    struct df_gvs_reply_queue queue, queue_before;
    struct df_gvs_send_transaction transaction, before;
    struct df_gvs_send_trace trace;
    struct scripted_sender sender = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 0));
    before = transaction;
    queue_before = queue;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_send_transaction_step(
                                               &transaction, &queue, 5,
                                               scripted_send, &sender,
                                               &trace));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &transaction, sizeof(transaction)));
    TEST_ASSERT_INT_EQ(0, memcmp(&queue_before, &queue, sizeof(queue)));
}

void test_gvs_send_transaction_finishes_timeout_near_clock_limit(void) {
    const uint64_t start = UINT64_MAX - DF_GVS_REPLY_QUEUE_TTL_MS;
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction;
    struct df_gvs_send_trace trace;
    struct scripted_sender sender = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, start));
    enqueue_reply(&queue, start, 3, 0x30);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_send_transaction_init(&transaction, start));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start + 250,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start + 350,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start + 600,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start + 700,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, start + 950,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_EVENT_TIMEOUT, trace.events[0].type);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_TIMEOUT,
                       transaction.last_outcome);

    /* A delayed dequeue may leave insufficient time for another attempt. */
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, start));
    enqueue_reply(&queue, start, 4, 0x40);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_send_transaction_init(&transaction, start));
    memset(&sender, 0, sizeof(sender));
    sender.results[0] = DF_GVS_SEND_ATTEMPT_FAILURE;
    sender.result_count = 1;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, UINT64_MAX - 250,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, UINT64_MAX - 150,
                                      scripted_send, &sender, &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_IDLE, transaction.state);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_TIMEOUT,
                       transaction.last_outcome);
}
