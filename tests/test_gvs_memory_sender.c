#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gvs_frame.h"
#include "gvs_memory_sender.h"
#include "test.h"

struct header_script {
    unsigned calls;
    unsigned failures;
};

static int scripted_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    struct header_script *script = context;
    size_t index;

    (void)request;
    script->calls++;
    if (script->calls <= script->failures) {
        return DF_ERR_INVALID;
    }
    for (index = 0; index < DF_GVS_HEADER_FIELD_SIZE; index++) {
        random_code[index] = (uint8_t)(index + 1U);
        encryption_code[index] = (uint8_t)(0xA1U + index);
    }
    return DF_OK;
}

static struct df_gvs_reply_queue_entry make_entry(void) {
    const struct df_gvs_reply_queue_entry entry = {
        .reply = {
            .target = {0x61, 2, 1, 1, 1, 3},
            .request_data = {0x12, 0x34},
            .peer_observed = true,
        },
        .expires_ms = 1000,
        .repeat_count = 2,
    };

    return entry;
}

void test_gvs_memory_sender_builds_and_records_exact_peer_reply(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t expected[DF_GVS_PEER_REPLY_FRAME_SIZE] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xA5, 0xA5, 0xA5, 0xA5,
        0x61, 2, 1, 1, 1, 3, 0x61, 2, 1, 1, 1, 2,
        1, 2, 3, 4, 5, 6, 7, 8,
        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
        0x07, 0x81, 0x06, 0x00, 0x12, 0x34, 0, 0, 0, 0,
    };
    struct header_script script = {0};
    struct df_gvs_memory_sender sender;
    struct df_gvs_reply_queue_entry entry = make_entry();
    struct df_gvs_frame parsed;
    struct df_event event;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_memory_sender_init(
                                      &sender, local, scripted_header_fields,
                                      &script));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_SUCCESS,
                       df_gvs_memory_send_attempt(&entry, 2, 41, &sender));
    TEST_ASSERT_INT_EQ(1, sender.record.valid);
    TEST_ASSERT_INT_EQ(48, (int)sender.record.length);
    TEST_ASSERT_INT_EQ(1, (int)sender.record.generation);
    TEST_ASSERT_INT_EQ(2, (int)sender.record.attempt);
    TEST_ASSERT_INT_EQ(41, (int)sender.record.completion_id);
    TEST_ASSERT_INT_EQ(0, memcmp(expected, sender.record.bytes,
                                 sizeof(expected)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(
                                      sender.record.bytes,
                                      sender.record.length, &parsed, &event));
    TEST_ASSERT_INT_EQ(0x07, parsed.family);
    TEST_ASSERT_INT_EQ(0x81, parsed.opcode);
    TEST_ASSERT_INT_EQ(6, parsed.payload_length);
}

void test_gvs_memory_sender_preserves_record_when_build_fails(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct header_script script = {0};
    struct df_gvs_memory_sender sender;
    struct df_gvs_memory_frame_record before;
    struct df_gvs_reply_queue_entry entry = make_entry();

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_memory_sender_init(
                                      &sender, local, scripted_header_fields,
                                      &script));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_SUCCESS,
                       df_gvs_memory_send_attempt(&entry, 1, 1, &sender));
    before = sender.record;
    script.failures = 2;
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_FAILURE,
                       df_gvs_memory_send_attempt(&entry, 2, 2, &sender));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &sender.record, sizeof(before)));
}

void test_gvs_memory_sender_drives_transaction_retry_to_success(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct header_script script = {.failures = 1};
    struct df_gvs_memory_sender sender;
    struct df_gvs_reply_queue queue;
    struct df_gvs_send_transaction transaction;
    struct df_gvs_send_trace trace;
    struct df_gvs_peer_reply reply = make_entry().reply;
    bool coalesced = true;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_memory_sender_init(
                                      &sender, local, scripted_header_fields,
                                      &script));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                      &queue, &reply, 0, &coalesced));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_init(&transaction, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 0,
                                      df_gvs_memory_send_attempt, &sender,
                                      &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_WAIT_RETRY, transaction.state);
    TEST_ASSERT_INT_EQ(0, sender.record.valid);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_send_transaction_step(
                                      &transaction, &queue, 100,
                                      df_gvs_memory_send_attempt, &sender,
                                      &trace));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_IDLE, transaction.state);
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_OUTCOME_SUCCESS,
                       transaction.last_outcome);
    TEST_ASSERT_INT_EQ(1, sender.record.valid);
    TEST_ASSERT_INT_EQ(2, (int)sender.record.attempt);
    TEST_ASSERT_INT_EQ(2, (int)sender.record.completion_id);
    TEST_ASSERT_INT_EQ(2, (int)script.calls);
}

void test_gvs_memory_sender_uses_explicit_zero_placeholder_fields(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t zero_fields[16] = {0};
    struct df_gvs_memory_sender sender;
    struct df_gvs_reply_queue_entry entry = make_entry();

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_memory_sender_init(
                   &sender, local, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_SUCCESS,
                       df_gvs_memory_send_attempt(&entry, 1, 1, &sender));
    TEST_ASSERT_INT_EQ(0, memcmp(zero_fields, sender.record.bytes + 22,
                                 sizeof(zero_fields)));
}
