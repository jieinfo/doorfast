#include "test.h"

#include <limits.h>
#include <string.h>

#include "gvs_elevator_control.h"

struct elevator_sender_record {
    int results[2];
    unsigned calls;
    struct df_gvs_elevator_request requests[2];
};

static int elevator_record_send(
    const struct df_gvs_elevator_request *request, void *context) {
    struct elevator_sender_record *record = context;
    unsigned index;

    if (request == NULL || record == NULL || record->calls >= 2U)
        return DF_ERR_INVALID;
    index = record->calls;
    record->requests[index] = *request;
    record->calls++;
    return record->results[index];
}

static struct df_gvs_frame elevator_completion(
    const struct df_gvs_elevator_control *control, const uint8_t local[6],
    uint8_t *payload, size_t payload_length) {
    struct df_gvs_frame frame = {
        .family = 0x08,
        .opcode = 0x82,
        .payload = payload,
        .payload_length = payload_length,
    };

    memcpy(frame.source, control->request.destination, 6);
    memcpy(frame.destination, local, 6);
    return frame;
}

void test_gvs_elevator_control_sends_twice_then_expires(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_sender_record sender = {{DF_OK, DF_OK}, 0, {{0}}};
    struct df_gvs_elevator_control control;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 0, elevator_record_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_DOWN, 7, 0));
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
    TEST_ASSERT_INT_EQ(1, (int)control.attempts);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_WAITING, control.state);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 999));
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 1000));
    TEST_ASSERT_INT_EQ(2, (int)sender.calls);
    TEST_ASSERT_INT_EQ(2, (int)control.attempts);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 1999));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_WAITING, control.state);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 2000));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_EXPIRED, control.state);
    TEST_ASSERT_INT_EQ(2, (int)sender.calls);
}

void test_gvs_elevator_control_completes_matching_reply_without_physical_claim(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_sender_record sender = {{DF_OK, DF_OK}, 0, {{0}}};
    struct df_gvs_elevator_control control;
    uint8_t unknown_payload[] = {0xaa, 0x55};
    struct df_gvs_frame frame;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 10, elevator_record_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_UP, 8, 10));
    frame = elevator_completion(
        &control, local, unknown_payload, sizeof(unknown_payload));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_observe(
        &control, local, &frame, 11));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_PROTOCOL_COMPLETED,
        control.state);
    TEST_ASSERT_INT_EQ(0, control.physical_result_confirmed);
    TEST_ASSERT_INT_EQ(0, strcmp("protocol_completed",
        df_gvs_elevator_control_state_name(control.state)));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 2000));
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
}

void test_gvs_elevator_control_rejects_duplicate_wrong_and_late_results(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_sender_record sender = {{DF_OK, DF_OK}, 0, {{0}}};
    struct df_gvs_elevator_control control;
    struct df_gvs_elevator_control snapshot;
    struct df_gvs_frame frame;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 0, elevator_record_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_DOWN, 9, 0));
    snapshot = control;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_UP, 10, 1));
    TEST_ASSERT_INT_EQ(0, memcmp(&snapshot, &control, sizeof(control)));
    frame = elevator_completion(&control, local, NULL, 0);
    frame.source[1]++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_control_observe(
        &control, local, &frame, 2));
    TEST_ASSERT_INT_EQ(0, memcmp(&snapshot, &control, sizeof(control)));
    frame.source[1]--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_control_observe(
        &control, local, &frame, 2000));
    TEST_ASSERT_INT_EQ(0, memcmp(&snapshot, &control, sizeof(control)));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 2000));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_EXPIRED, control.state);
}

void test_gvs_elevator_control_distinguishes_bounded_send_failures(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_sender_record recovers = {{DF_ERR_IO, DF_OK}, 0, {{0}}};
    struct elevator_sender_record fails = {{DF_ERR_IO, DF_ERR_IO}, 0, {{0}}};
    struct df_gvs_elevator_control control;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 0, elevator_record_send, &recovers));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_DOWN, 11, 0));
    TEST_ASSERT_INT_EQ(0, (int)control.successful_sends);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 1000));
    TEST_ASSERT_INT_EQ(1, (int)control.successful_sends);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 2000));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_EXPIRED, control.state);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 3000, elevator_record_send, &fails));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_UP, 12, 3000));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 4000));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_SEND_FAILED, control.state);
    TEST_ASSERT_INT_EQ(2, (int)fails.calls);
}

void test_gvs_elevator_control_preserves_time_and_cancels_changed_identity(void) {
    uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_sender_record sender = {{DF_OK, DF_OK}, 0, {{0}}};
    struct df_gvs_elevator_control control;
    struct df_gvs_elevator_control snapshot;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &control, 100, elevator_record_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_DOWN, 13, 100));
    snapshot = control;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_control_tick(&control, local, 99));
    TEST_ASSERT_INT_EQ(0, memcmp(&snapshot, &control, sizeof(control)));
    local[5]++;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_control_tick(&control, local, 101));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_CANCELLED, control.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_control_submit(
        &control, local, DF_GVS_ELEVATOR_DOWN, 14, UINT64_MAX - 1000U));
}
