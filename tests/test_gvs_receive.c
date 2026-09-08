#include <string.h>

#include "gvs_receive.h"
#include "test.h"

static size_t make_receive_frame(uint8_t packet[64], const uint8_t destination[6],
                                 const uint8_t source[6], uint8_t opcode,
                                 const uint8_t *payload, uint16_t payload_length) {
    static const uint8_t header[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5
    };

    memset(packet, 0, 64);
    memcpy(packet, header, sizeof(header));
    memcpy(packet + 10, destination, 6);
    memcpy(packet + 16, source, 6);
    packet[38] = 3;
    packet[39] = opcode;
    packet[40] = (uint8_t)(payload_length & 0xffU);
    packet[41] = (uint8_t)(payload_length >> 8U);
    if (payload_length != 0) {
        memcpy(packet + 42, payload, payload_length);
    }
    return 42U + payload_length;
}

void test_gvs_receive_drives_call_pick_sync_and_timeout(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t pick_payload[7] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
    const uint8_t sync_payload[1] = {1};
    uint8_t packet[64];
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_receive_result result;
    bool timed_out = false;
    size_t length;

    length = make_receive_frame(packet, local, peer, 1, NULL, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(packet, length, local,
        &session, &deadline, 100, &result));
    TEST_ASSERT_INT_EQ(1, result.accepted_call);
    TEST_ASSERT_INT_EQ(1, result.transition.count);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(1, deadline.armed);

    length = make_receive_frame(packet, peer, local, 3, pick_payload, 7);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(packet, length, local,
        &session, &deadline, 200, &result));
    TEST_ASSERT_INT_EQ(0, result.talking_transition);

    length = make_receive_frame(packet, local, peer, 0x83, pick_payload, 7);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(packet, length, local,
        &session, &deadline, 250, &result));
    TEST_ASSERT_INT_EQ(1, result.talking_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);

    length = make_receive_frame(packet, local, peer, 0x57, sync_payload, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(packet, length, local,
        &session, &deadline, 300, &result));
    TEST_ASSERT_INT_EQ(1, result.time_sync_update);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_tick(&deadline, &session, 1300, &timed_out));
    TEST_ASSERT_INT_EQ(1, timed_out);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
}

void test_gvs_receive_applies_peer_hangup_and_cancels_deadline(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t payload[1] = {1};
    uint8_t packet[64];
    struct df_gvs_session session = {
        .state = DF_GVS_TALKING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 9,
    };
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_receive_result result;
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 100, 120000));
    length = make_receive_frame(packet, local, peer, 2, payload, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(packet, length, local,
        &session, &deadline, 200, &result));
    TEST_ASSERT_INT_EQ(1, result.ended_transition);
    TEST_ASSERT_INT_EQ(1, result.observed_hangup);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
}
