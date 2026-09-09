#include <string.h>

#include "gvs_call_control.h"
#include "gvs_memory_sender.h"
#include "test.h"

static size_t control_reply(uint8_t out[64], const uint8_t destination[6],
    const uint8_t source[6], uint8_t opcode, const uint8_t *payload,
    size_t payload_length) {
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5
    };

    memset(out, 0, 64);
    memcpy(out, magic, sizeof(magic));
    memcpy(out + 10, destination, 6);
    memcpy(out + 16, source, 6);
    out[38] = 0x03;
    out[39] = opcode;
    out[40] = (uint8_t)payload_length;
    if (payload_length != 0U) {
        memcpy(out + 42, payload, payload_length);
    }
    return 42U + payload_length;
}

static int reject_control_header(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)random_code;
    (void)encryption_code;
    (void)context;
    return DF_ERR_INVALID;
}

void test_gvs_call_control_answer_reaches_confirmation(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t answer_reply[7] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 0x1e};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 9,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 9, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    TEST_ASSERT_INT_EQ(49, control.sender.length);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING,
                       control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(0x03, control.sender.bytes[39]);

    length = control_reply(packet, local, session.peer, 0x83,
                           answer_reply, sizeof(answer_reply));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.talking_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
}

void test_gvs_call_control_reports_confirmation_timeout_once(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 4,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_hangup(
        &control, &session, 4, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline,
        DF_GVS_CALL_CONFIRM_TIMEOUT_MS, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_expired);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline,
        DF_GVS_CALL_CONFIRM_TIMEOUT_MS + 1U, &result));
    TEST_ASSERT_INT_EQ(0, result.runtime.acknowledgement_expired);
}

void test_gvs_call_control_rejects_busy_and_stale_submission(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_call_control control;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_submit_answer(
        &control, &session, 6, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_call_control_submit_hangup(
        &control, &session, 7, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_QUEUED, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_EMPTY,
                       control.acknowledgement.state);
}

void test_gvs_call_control_bounds_failed_simulated_delivery(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, reject_control_header, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 100, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 200, &result));
    TEST_ASSERT_INT_EQ(1, result.dispatch_failed);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAILED, control.dispatch.state);
    TEST_ASSERT_INT_EQ(3, control.dispatch.attempts);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_EMPTY,
                       control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(0, control.sender.length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 201, &result));
    TEST_ASSERT_INT_EQ(0, result.dispatch_failed);
}

void test_gvs_call_control_rejects_receive_before_submission_time(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control, before_control;
    struct df_gvs_session before_session;
    struct df_gvs_deadline before_deadline;
    struct df_gvs_call_control_result result = {.frame_ready = true};
    uint8_t packet[64];
    size_t length;

    memcpy(session.peer, peer, sizeof(peer));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 100));
    length = control_reply(packet, local, peer, 0x02, NULL, 0);
    before_control = control;
    before_session = session;
    before_deadline = deadline;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 50, &result));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_control, &control, sizeof(control)));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_session, &session, sizeof(session)));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_deadline, &deadline, sizeof(deadline)));
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
}

void test_gvs_call_control_exposes_public_status(void) {
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    control.dispatch.state = DF_GVS_CALL_RETRY;
    control.dispatch.command.type = DF_GVS_CALL_COMMAND_ANSWER;
    control.dispatch.attempts = 2;
    control.acknowledgement.state = DF_GVS_CALL_ACK_EMPTY;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, status.session_state);
    TEST_ASSERT_INT_EQ(7, status.session_generation);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, status.command_type);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, status.dispatch_state);
    TEST_ASSERT_INT_EQ(2, status.attempts);
    TEST_ASSERT_INT_EQ(0, strcmp("ringing",
        df_gvs_session_state_name(status.session_state)));
    TEST_ASSERT_INT_EQ(0, strcmp("answer",
        df_gvs_call_command_type_name(status.command_type)));
    TEST_ASSERT_INT_EQ(0, strcmp("retry",
        df_gvs_call_dispatch_state_name(status.dispatch_state)));
    TEST_ASSERT_INT_EQ(0, strcmp("empty",
        df_gvs_call_ack_state_name(status.acknowledgement_state)));
    TEST_ASSERT_INT_EQ(1, df_gvs_session_state_name(
        (enum df_gvs_session_state)99) == NULL);
}
