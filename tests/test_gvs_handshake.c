#include <stdint.h>
#include <string.h>

#include "gvs_frame.h"
#include "gvs_handshake.h"
#include "gvs_memory_sender.h"
#include "test.h"

static struct df_gvs_session handshake_session(void) {
    const struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };

    return session;
}

static size_t make_handshake_frame(uint8_t packet[42],
                                   const uint8_t destination[6],
                                   const uint8_t source[6], uint8_t opcode) {
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5,
    };

    memset(packet, 0, 42);
    memcpy(packet, magic, sizeof(magic));
    memcpy(packet + 10, destination, 6);
    memcpy(packet + 16, source, 6);
    packet[38] = 0x03;
    packet[39] = opcode;
    return 42U;
}

static void assert_handshake_frame(const struct df_gvs_handshake_action *action,
                                   uint8_t expected_opcode) {
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t bytes[DF_GVS_HANDSHAKE_FRAME_SIZE];
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_action_serialize(
        action, bytes, sizeof(bytes), &length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(42, (int)length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(bytes, length, &frame, &event));
    TEST_ASSERT_INT_EQ(0x03, frame.family);
    TEST_ASSERT_INT_EQ(expected_opcode, frame.opcode);
    TEST_ASSERT_INT_EQ(0, frame.payload_length);
}

void test_gvs_handshake_sends_five_probes_then_disconnects(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint64_t times[] = {100, 2100, 4100, 6100, 8100};
    struct df_gvs_session session = handshake_session();
    struct df_gvs_handshake handshake;
    struct df_gvs_handshake_result result;
    size_t index;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_start(
        &handshake, &session, local, 100));
    for (index = 0; index < sizeof(times) / sizeof(times[0]); ++index) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
            &handshake, &session, local, times[index], &result));
        TEST_ASSERT_INT_EQ(1, result.action.valid);
        TEST_ASSERT_INT_EQ(DF_GVS_HANDSHAKE_ASK, result.action.type);
        TEST_ASSERT_INT_EQ((int)index + 1, (int)handshake.missed_replies);
        TEST_ASSERT_INT_EQ(0, result.disconnected);
        assert_handshake_frame(&result.action, 0x51);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 10100, &result));
    TEST_ASSERT_INT_EQ(0, result.action.valid);
    TEST_ASSERT_INT_EQ(1, result.disconnected);
    TEST_ASSERT_INT_EQ(0, handshake.active);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 12100, &result));
    TEST_ASSERT_INT_EQ(0, result.disconnected);
}

void test_gvs_handshake_reply_resets_misses_without_moving_schedule(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = handshake_session();
    struct df_gvs_handshake handshake;
    struct df_gvs_handshake_result result;
    uint8_t packet[42];

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_start(
        &handshake, &session, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 0, &result));
    TEST_ASSERT_INT_EQ(1, handshake.missed_replies);
    (void)make_handshake_frame(packet, local, session.peer, 0x52);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_receive(
        &handshake, packet, sizeof(packet), &session, local, 1000, &result));
    TEST_ASSERT_INT_EQ(1, result.accepted_reply);
    TEST_ASSERT_INT_EQ(0, handshake.missed_replies);
    TEST_ASSERT_INT_EQ(2000, (int)handshake.next_probe_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 1999, &result));
    TEST_ASSERT_INT_EQ(0, result.action.valid);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 2000, &result));
    TEST_ASSERT_INT_EQ(1, result.action.valid);
    TEST_ASSERT_INT_EQ(1, handshake.missed_replies);
}

void test_gvs_handshake_ask_prepares_reply_and_restarts_schedule(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = handshake_session();
    struct df_gvs_handshake handshake;
    struct df_gvs_handshake_result result;
    uint8_t packet[42];

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_start(
        &handshake, &session, local, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 100, &result));
    (void)make_handshake_frame(packet, local, session.peer, 0x51);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_receive(
        &handshake, packet, sizeof(packet), &session, local, 1500, &result));
    TEST_ASSERT_INT_EQ(1, result.accepted_ask);
    TEST_ASSERT_INT_EQ(DF_GVS_HANDSHAKE_REPLY, result.action.type);
    TEST_ASSERT_INT_EQ(0, handshake.missed_replies);
    TEST_ASSERT_INT_EQ(3500, (int)handshake.next_probe_ms);
    assert_handshake_frame(&result.action, 0x52);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 2100, &result));
    TEST_ASSERT_INT_EQ(0, result.action.valid);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_tick(
        &handshake, &session, local, 3500, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_HANDSHAKE_ASK, result.action.type);
}

void test_gvs_handshake_rejects_wrong_peer_stale_generation_and_clock_limit(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = handshake_session();
    struct df_gvs_handshake handshake;
    struct df_gvs_handshake before;
    struct df_gvs_handshake_result result;
    uint8_t packet[42];

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_start(
        &handshake, &session, local, 100));
    (void)make_handshake_frame(packet, local,
        (const uint8_t[6]){0x32, 2, 1, 0, 2, 0}, 0x52);
    before = handshake;
    memset(&result, 0xa5, sizeof(result));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_handshake_receive(
        &handshake, packet, sizeof(packet), &session, local, 200, &result));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &handshake, sizeof(handshake)));
    TEST_ASSERT_INT_EQ(0, result.action.valid);
    TEST_ASSERT_INT_EQ(0, result.accepted_reply);
    session.generation++;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_handshake_receive(
        &handshake, packet, sizeof(packet), &session, local, 300, &result));
    TEST_ASSERT_INT_EQ(0, handshake.active);
    TEST_ASSERT_INT_EQ(0, result.disconnected);

    session = handshake_session();
    memset(&handshake, 0xa5, sizeof(handshake));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_handshake_start(
        &handshake, &session, local, UINT64_MAX - 1999U));
    TEST_ASSERT_INT_EQ(0, handshake.active);
}
