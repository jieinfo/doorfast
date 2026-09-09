#include <string.h>

#include "gvs_frame.h"
#include "gvs_serialize.h"
#include "test.h"

static int fixed_header_fields(uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
                               uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE],
                               void *context) {
    size_t index;

    (void)context;
    for (index = 0; index < DF_GVS_HEADER_FIELD_SIZE; index++) {
        random_code[index] = (uint8_t)(index + 1U);
        encryption_code[index] = (uint8_t)(0xA1U + index);
    }
    return DF_OK;
}

static int reject_header_fields(uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
                                uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE],
                                void *context) {
    (void)random_code;
    (void)encryption_code;
    (void)context;
    return DF_ERR_INVALID;
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
