#include <string.h>

#include "gvs_frame.h"
#include "gvs_serialize.h"
#include "test.h"

static int fixed_header_fields(const struct df_gvs_header_request *request,
                               uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
                               uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE],
                               void *context) {
    size_t index;

    (void)request;
    (void)context;
    for (index = 0; index < DF_GVS_HEADER_FIELD_SIZE; index++) {
        random_code[index] = (uint8_t)(index + 1U);
        encryption_code[index] = (uint8_t)(0xA1U + index);
    }
    return DF_OK;
}

static int reject_header_fields(const struct df_gvs_header_request *request,
                                uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
                                uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE],
                                void *context) {
    (void)request;
    (void)context;
    memset(random_code, 0xEE, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0xFF, DF_GVS_HEADER_FIELD_SIZE);
    return DF_ERR_INVALID;
}

struct observed_header_request {
    unsigned calls;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t family;
    uint8_t opcode;
    uint8_t payload[8];
    uint16_t payload_length;
    bool payload_was_null;
};

static int observe_header_request(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    struct observed_header_request *observed = context;

    if (request == NULL || request->destination == NULL ||
        request->source == NULL || observed == NULL ||
        request->payload_length > sizeof(observed->payload) ||
        (request->payload_length > 0U && request->payload == NULL)) {
        return DF_ERR_INVALID;
    }
    observed->calls++;
    memcpy(observed->destination, request->destination,
           sizeof(observed->destination));
    memcpy(observed->source, request->source, sizeof(observed->source));
    observed->family = request->family;
    observed->opcode = request->opcode;
    observed->payload_length = request->payload_length;
    observed->payload_was_null = request->payload == NULL;
    if (request->payload_length > 0U) {
        memcpy(observed->payload, request->payload, request->payload_length);
    }
    memset(random_code, 0x31, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x41, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

void test_gvs_serialize_builds_complete_peer_probe_frame(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t target[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x02};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PEER_PROBE,
        .target = {0x61, 0x02, 0x01, 0x01, 0x01, 0x02},
    };
    struct df_gvs_frame frame = {0};
    struct df_event event = {0};
    uint8_t packet[64] = {0};
    size_t length = 0;

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_presence_action_serialize(
                   &action, local, 1, packet, sizeof(packet), &length,
                   fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(44, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp("GVSGVS\xA5\xA5\xA5\xA5", packet, 10));
    TEST_ASSERT_INT_EQ(0, memcmp(target, packet + 10, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(local, packet + 16, 6));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 8},
                                 packet + 22, 8));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0xA1, 0xA2, 0xA3, 0xA4,
                                                   0xA5, 0xA6, 0xA7, 0xA8},
                                 packet + 30, 8));
    TEST_ASSERT_INT_EQ(0x07, packet[38]);
    TEST_ASSERT_INT_EQ(0x01, packet[39]);
    TEST_ASSERT_INT_EQ(2, packet[40]);
    TEST_ASSERT_INT_EQ(0, packet[41]);
    TEST_ASSERT_INT_EQ(0, packet[42]);
    TEST_ASSERT_INT_EQ(1, packet[43]);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_frame_parse(packet, length, &frame, &event));
    TEST_ASSERT_INT_EQ(2, frame.payload_length);
}

void test_gvs_serialize_builds_sync_ask_and_version_ask(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_SYNC_ASK_ACTION,
        .target = {0x61, 0x02, 0x01, 0x01, 0x01, 0x02},
        .round = 2,
    };
    uint8_t packet[64] = {0};
    size_t length = 0;

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_presence_action_serialize(
                   &action, local, 0x1234, packet, sizeof(packet), &length,
                   fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(45, (int)length);
    TEST_ASSERT_INT_EQ(0x91, packet[38]);
    TEST_ASSERT_INT_EQ(0x01, packet[39]);
    TEST_ASSERT_INT_EQ(3, packet[40]);
    TEST_ASSERT_INT_EQ(0x34, packet[42]);
    TEST_ASSERT_INT_EQ(0x12, packet[43]);
    TEST_ASSERT_INT_EQ(2, packet[44]);

    action.type = DF_GVS_PRESENCE_SYNC_VERSION_ASK;
    action.round = 0;
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_presence_action_serialize(
                   &action, local, 0x1234, packet, sizeof(packet), &length,
                   fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(42, (int)length);
    TEST_ASSERT_INT_EQ(0x91, packet[38]);
    TEST_ASSERT_INT_EQ(0x02, packet[39]);
    TEST_ASSERT_INT_EQ(0, packet[40]);
    TEST_ASSERT_INT_EQ(0, packet[41]);
}

void test_gvs_serialize_builds_peer_reply_from_request_data(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t target[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t expected_payload[6] = {0x12, 0x34, 0, 0, 0, 0};
    const struct df_gvs_peer_reply reply = {
        .target = {0x61, 2, 1, 1, 1, 1},
        .request_data = {0x12, 0x34},
        .peer_observed = true,
    };
    uint8_t packet[64] = {0};
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_reply_serialize(
        &reply, local, packet, sizeof(packet), &length,
        fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(48, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(target, packet + 10, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(local, packet + 16, 6));
    TEST_ASSERT_INT_EQ(0x07, packet[38]);
    TEST_ASSERT_INT_EQ(0x81, packet[39]);
    TEST_ASSERT_INT_EQ(6, packet[40]);
    TEST_ASSERT_INT_EQ(0, memcmp(expected_payload, packet + 42, 6));

    length = 99;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_peer_reply_serialize(
        &reply, local, packet, 47, &length, fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
    length = 99;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_peer_reply_serialize(
        &reply, local, packet, sizeof(packet), &length,
        reject_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
}

void test_gvs_serialize_rejects_missing_header_fields_and_unsupported_actions(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PEER_PROBE,
        .target = {0x61, 0x02, 0x01, 0x01, 0x01, 0x02},
    };
    uint8_t packet[64] = {0};
    size_t length = 99;

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_presence_action_serialize(
                            &action, local, 1, packet, sizeof(packet), &length,
                            NULL, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_presence_action_serialize(
                            &action, local, 1, packet, sizeof(packet), &length,
                            reject_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_presence_action_serialize(
                            &action, local, 1, packet, 43, &length,
                            fixed_header_fields, NULL));
    action.type = DF_GVS_PRESENCE_PERIODIC_SYNC;
    length = 99;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_presence_action_serialize(
                            &action, local, 1, packet, sizeof(packet), &length,
                            fixed_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
}

void test_gvs_header_provider_receives_complete_read_only_frame_contract(void) {
    const uint8_t destination[6] = {0x61, 2, 1, 1, 1, 3};
    const uint8_t source[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t payload[6] = {0x12, 0x34, 0, 0, 0, 0};
    struct observed_header_request observed = {0};
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE + sizeof(payload)];
    size_t length = 0;

    memset(packet, 0xCC, sizeof(packet));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_control_serialize(
                   packet, sizeof(packet), &length, destination, source,
                   0x07, 0x81, payload, sizeof(payload),
                   observe_header_request, &observed));
    TEST_ASSERT_INT_EQ(1, (int)observed.calls);
    TEST_ASSERT_INT_EQ(0, memcmp(destination, observed.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(source, observed.source, 6));
    TEST_ASSERT_INT_EQ(0x07, observed.family);
    TEST_ASSERT_INT_EQ(0x81, observed.opcode);
    TEST_ASSERT_INT_EQ(6, observed.payload_length);
    TEST_ASSERT_INT_EQ(0, observed.payload_was_null);
    TEST_ASSERT_INT_EQ(0, memcmp(payload, observed.payload, sizeof(payload)));
    TEST_ASSERT_INT_EQ(0, memcmp(packet + 22,
                                 (const uint8_t[]){0x31, 0x31, 0x31, 0x31,
                                                   0x31, 0x31, 0x31, 0x31},
                                 DF_GVS_HEADER_FIELD_SIZE));
    TEST_ASSERT_INT_EQ(0, memcmp(packet + 30,
                                 (const uint8_t[]){0x41, 0x41, 0x41, 0x41,
                                                   0x41, 0x41, 0x41, 0x41},
                                 DF_GVS_HEADER_FIELD_SIZE));

    memset(&observed, 0, sizeof(observed));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_control_serialize(
                   packet, sizeof(packet), &length, destination, source,
                   0x91, 0x02, NULL, 0, observe_header_request, &observed));
    TEST_ASSERT_INT_EQ(1, (int)observed.calls);
    TEST_ASSERT_INT_EQ(0x91, observed.family);
    TEST_ASSERT_INT_EQ(0x02, observed.opcode);
    TEST_ASSERT_INT_EQ(0, observed.payload_length);
    TEST_ASSERT_INT_EQ(1, observed.payload_was_null);
}

void test_gvs_header_provider_failure_is_output_atomic(void) {
    const uint8_t destination[6] = {0x61, 2, 1, 1, 1, 3};
    const uint8_t source[6] = {0x61, 2, 1, 1, 1, 2};
    uint8_t before[48];
    uint8_t packet[48];
    size_t length = 99;

    memset(before, 0x5A, sizeof(before));
    memcpy(packet, before, sizeof(packet));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_control_serialize(
                            packet, sizeof(packet), &length, destination,
                            source, 0x07, 0x81,
                            (const uint8_t[]){0x12, 0x34, 0, 0, 0, 0}, 6,
                            reject_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(before, packet, sizeof(packet)));
}
