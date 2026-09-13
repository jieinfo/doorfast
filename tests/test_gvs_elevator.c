#include "test.h"

#include <string.h>

#include "gvs_elevator.h"
#include "gvs_frame.h"
#include "gvs_memory_sender.h"

static int elevator_failing_header(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)random_code;
    (void)encryption_code;
    (void)context;
    return DF_ERR_IO;
}

void test_gvs_elevator_call_matches_two_digit_floor_capture(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    const uint8_t expected_destination[6] = {0x35, 0x02, 0x01, 0, 1, 0};
    const uint8_t expected_payload[4] = {0x00, 0x10, 0x16, 0x01};
    struct df_gvs_elevator_request request;
    uint8_t wire[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
    size_t wire_length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    TEST_ASSERT_INT_EQ(0, memcmp(request.destination,
        expected_destination, sizeof(expected_destination)));
    TEST_ASSERT_INT_EQ(0, memcmp(request.payload,
        expected_payload, sizeof(expected_payload)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_serialize(
        &request, wire, sizeof(wire), &wire_length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(46, (int)wire_length);
    TEST_ASSERT_INT_EQ(0x08, wire[38]);
    TEST_ASSERT_INT_EQ(0x02, wire[39]);
    TEST_ASSERT_INT_EQ(4, wire[40]);
    TEST_ASSERT_INT_EQ(0, wire[41]);
    TEST_ASSERT_INT_EQ(0, memcmp(wire + 42, expected_payload, 4));
}

void test_gvs_elevator_call_matches_single_digit_capture_directions(void) {
    const uint8_t local[6] = {0x61, 0x13, 0x01, 0x06, 0x01, 0x01};
    const uint8_t down[4] = {0x00, 0x06, 0x06, 0x01};
    const uint8_t up[4] = {0x01, 0x06, 0x06, 0x01};
    struct df_gvs_elevator_request request;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    TEST_ASSERT_INT_EQ(0, memcmp(request.payload, down, sizeof(down)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_UP, &request));
    TEST_ASSERT_INT_EQ(0, memcmp(request.payload, up, sizeof(up)));
}

void test_gvs_elevator_query_has_exact_empty_shape(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    struct df_gvs_elevator_request request;
    uint8_t wire[DF_GVS_ELEVATOR_QUERY_FRAME_SIZE];
    size_t wire_length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_query(local, &request));
    TEST_ASSERT_INT_EQ(0x03, request.opcode);
    TEST_ASSERT_INT_EQ(0, request.payload_length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_serialize(
        &request, wire, sizeof(wire), &wire_length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(42, (int)wire_length);
    TEST_ASSERT_INT_EQ(0x08, wire[38]);
    TEST_ASSERT_INT_EQ(0x03, wire[39]);
    TEST_ASSERT_INT_EQ(0, wire[40]);
    TEST_ASSERT_INT_EQ(0, wire[41]);
}

void test_gvs_elevator_rejects_invalid_identity_and_direction(void) {
    uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    struct df_gvs_elevator_request request;
    static const struct df_gvs_elevator_request empty = {0};

    local[0] = 0x62;
    memset(&request, 0xa5, sizeof(request));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    TEST_ASSERT_INT_EQ(0, memcmp(&request, &empty, sizeof(empty)));
    local[0] = 0x61;
    local[3] = 0x1a;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    local[3] = 0x16;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_prepare_call(
        local, (enum df_gvs_elevator_direction)2, &request));
    memset(local, 0, sizeof(local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_prepare_query(local, &request));
}

void test_gvs_elevator_serialize_failure_is_output_atomic(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    struct df_gvs_elevator_request request;
    uint8_t wire[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
    uint8_t expected[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
    size_t wire_length = 99;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    memset(wire, 0xa5, sizeof(wire));
    memcpy(expected, wire, sizeof(expected));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_serialize(
        &request, wire, sizeof(wire) - 1U, &wire_length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, wire_length);
    TEST_ASSERT_INT_EQ(0, memcmp(wire, expected, sizeof(wire)));
    wire_length = 99;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_serialize(
        &request, wire, sizeof(wire), &wire_length,
        elevator_failing_header, NULL));
    TEST_ASSERT_INT_EQ(0, wire_length);
    TEST_ASSERT_INT_EQ(0, memcmp(wire, expected, sizeof(wire)));
}

static struct df_gvs_frame elevator_status_frame(
    const uint8_t local[6], uint8_t *payload, size_t payload_length) {
    struct df_gvs_frame frame = {
        .family = 0x08,
        .opcode = 0x83,
        .payload = payload,
        .payload_length = payload_length,
    };

    frame.source[0] = 0x35;
    frame.source[1] = local[1];
    frame.source[2] = local[2];
    frame.source[4] = 1;
    memcpy(frame.destination, local, 6);
    return frame;
}

void test_gvs_elevator_status_parses_entries_and_extensions(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    uint8_t payload[] = {2, 5, 1, 6, 3, 0xaa};
    struct df_gvs_frame frame = elevator_status_frame(
        local, payload, sizeof(payload));
    struct df_gvs_elevator_status status;

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_parse_status(&frame, local, &status));
    TEST_ASSERT_INT_EQ(1, status.valid);
    TEST_ASSERT_INT_EQ(2, (int)status.count);
    TEST_ASSERT_INT_EQ(1, (int)status.extension_length);
    TEST_ASSERT_INT_EQ(5, status.entries[0].floor);
    TEST_ASSERT_INT_EQ(1, status.entries[0].raw_state);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_MOVING_UP,
        status.entries[0].motion);
    TEST_ASSERT_INT_EQ(6, status.entries[1].floor);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_STOPPED,
        status.entries[1].motion);
    TEST_ASSERT_INT_EQ(0, strcmp("moving_up",
        df_gvs_elevator_motion_name(status.entries[0].motion)));
}

void test_gvs_elevator_status_decodes_vendor_negative_floors(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    uint8_t payload[] = {3, 0x80, 0, 0x81, 2, 0xff, 0x7f};
    struct df_gvs_frame frame = elevator_status_frame(
        local, payload, sizeof(payload));
    struct df_gvs_elevator_status status;

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_elevator_parse_status(&frame, local, &status));
    TEST_ASSERT_INT_EQ(0, status.entries[0].floor);
    TEST_ASSERT_INT_EQ(-1, status.entries[1].floor);
    TEST_ASSERT_INT_EQ(-127, status.entries[2].floor);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_FAULT,
        status.entries[0].motion);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_MOVING_DOWN,
        status.entries[1].motion);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_OTHER,
        status.entries[2].motion);
    TEST_ASSERT_INT_EQ(0, strcmp("other",
        df_gvs_elevator_motion_name(status.entries[2].motion)));
}

void test_gvs_elevator_status_rejects_malformed_counts(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    uint8_t truncated[] = {2, 1, 0};
    uint8_t too_many[] = {9, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 0};
    struct df_gvs_frame frame = elevator_status_frame(local, NULL, 0);
    struct df_gvs_elevator_status status;
    static const struct df_gvs_elevator_status empty = {0};

    memset(&status, 0xa5, sizeof(status));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    TEST_ASSERT_INT_EQ(0, memcmp(&status, &empty, sizeof(status)));
    frame = elevator_status_frame(local, truncated, sizeof(truncated));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    frame = elevator_status_frame(local, too_many, sizeof(too_many));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
}

void test_gvs_elevator_status_rejects_wrong_frame_or_route(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
    uint8_t payload[] = {1, 5, 1};
    struct df_gvs_frame frame = elevator_status_frame(
        local, payload, sizeof(payload));
    struct df_gvs_elevator_status status;
    static const struct df_gvs_elevator_status empty = {0};

    frame.family = 0x03;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    frame.family = 0x08;
    frame.opcode = 0x82;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    frame.opcode = 0x83;
    frame.source[1]++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    frame.source[1]--;
    frame.destination[5]++;
    memset(&status, 0xa5, sizeof(status));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_elevator_parse_status(&frame, local, &status));
    TEST_ASSERT_INT_EQ(0, memcmp(&status, &empty, sizeof(status)));
}
