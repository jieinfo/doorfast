#include <stdbool.h>
#include <string.h>

#include "gvs_identity.h"
#include "gvs_observer.h"
#include "test.h"

static void make_incoming_call(uint8_t packet[42], uint8_t room_low_bcd) {
    static const uint8_t header[] = {'G', 'V', 'S', 'G', 'V', 'S',
                                     0xa5, 0xa5, 0xa5, 0xa5};

    memset(packet, 0, 42);
    memcpy(packet, header, sizeof(header));
    memcpy(packet + 10, (const uint8_t[]){0x61, 0x02, 0x01, 0x01, room_low_bcd, 0x00}, 6);
    memcpy(packet + 16, (const uint8_t[]){0x32, 0x02, 0x01, 0x00, 0x01, 0x00}, 6);
    packet[38] = 0x03;
    packet[39] = 0x01;
}

void test_gvs_observer_only_starts_a_session_for_the_configured_identity(void) {
    uint8_t identity[6];
    uint8_t packet[42];
    struct df_gvs_session session = {0};
    struct df_event event = {0};
    bool accepted = true;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    make_incoming_call(packet, 0x01);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_observe_datagram(packet, sizeof(packet), identity,
                                                       &session, &event, &accepted));
    TEST_ASSERT_INT_EQ(1, accepted);
    TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, event.type);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);

    session.state = DF_GVS_IDLE;
    accepted = true;
    make_incoming_call(packet, 0x02);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_observe_datagram(packet, sizeof(packet), identity,
                                                       &session, &event, &accepted));
    TEST_ASSERT_INT_EQ(0, accepted);
    TEST_ASSERT_INT_EQ(DF_GVS_IDLE, session.state);
}

void test_gvs_observer_batch_preemption(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t old[6] = {0x61, 2, 1, 1, 1, 2};
    uint8_t packet[42];
    struct df_gvs_session session = {0};
    struct df_gvs_observation result;
    make_incoming_call(packet, 1);
    memcpy(packet + 16, old, 6);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_observe_datagram_batch(packet, sizeof(packet),
        local, &session, 17, &result));
    TEST_ASSERT_INT_EQ(1, result.accepted);
    TEST_ASSERT_INT_EQ(1, result.transition.count);
    TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, result.transition.events[0].type);
    TEST_ASSERT_INT_EQ(0, memcmp(result.transition.events[0].peer, old, 6));
    TEST_ASSERT_INT_EQ(1, result.transition.events[0].generation == 1);
    TEST_ASSERT_INT_EQ(1, result.transition.ring_started_ms == 17);
    make_incoming_call(packet, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_observe_datagram_batch(packet, sizeof(packet),
        local, &session, 29000, &result));
    TEST_ASSERT_INT_EQ(2, result.transition.count);
    TEST_ASSERT_INT_EQ(DF_EVENT_HANGUP, result.transition.events[0].type);
    TEST_ASSERT_INT_EQ(0, memcmp(result.transition.events[0].peer, old, 6));
    TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, result.transition.events[1].type);
    TEST_ASSERT_INT_EQ(0, memcmp(result.transition.events[1].peer, packet + 16, 6));
    TEST_ASSERT_INT_EQ(1, result.transition.ring_started_ms == 29000);
    struct df_gvs_session saved = session;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_observe_datagram_batch(packet, sizeof(packet),
        local, &session, 30000, &result));
    TEST_ASSERT_INT_EQ(1, result.accepted);
    TEST_ASSERT_INT_EQ(0, result.transition.count);
    TEST_ASSERT_INT_EQ(0, memcmp(&saved, &session, sizeof(saved)));
    /* Low priority, unknown category, wrong target, malformed and non-call. */
    for (int scenario = 0; scenario < 5; ++scenario) {
        make_incoming_call(packet, 1);
        memcpy(packet + 16, old, 6);
        if (scenario == 1) packet[16] = 0xff;
        if (scenario == 2) packet[14] = 2;
        if (scenario == 3) packet[40] = 1;
        if (scenario == 4) packet[39] = 0x81;
        int expected = scenario == 2 || scenario == 4 ? DF_OK : DF_ERR_INVALID;
        memset(&result, 0xff, sizeof(result));
        TEST_ASSERT_INT_EQ(expected, df_gvs_observe_datagram_batch(packet, sizeof(packet),
            local, &session, 31000, &result));
        TEST_ASSERT_INT_EQ(0, result.accepted);
        TEST_ASSERT_INT_EQ(0, result.transition.count);
        TEST_ASSERT_INT_EQ(0, memcmp(&saved, &session, sizeof(saved)));
    }
    /* Legacy single-event caller must not silently discard an old HANGUP. */
    session = (struct df_gvs_session){.state = DF_GVS_RINGING, .generation = 1};
    memcpy(session.peer, old, 6);
    make_incoming_call(packet, 1);
    struct df_event event;
    bool accepted;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_observe_datagram(packet, sizeof(packet),
        local, &session, &event, &accepted));
    TEST_ASSERT_INT_EQ(0, accepted);
    for (int scenario = 0; scenario < 4; ++scenario) {
        session = (struct df_gvs_session){.state = DF_GVS_RINGING, .generation = 1};
        memcpy(session.peer, old, 6);
        if (scenario == 0) session.peer[0] = 0xff;
        if (scenario == 1) session.peer[0] = 0x31; /* Same rank as incoming 0x32. */
        if (scenario >= 2) {
            session.state = scenario == 2 ? DF_GVS_IDLE : DF_GVS_ENDED;
            session.generation = UINT64_MAX;
        }
        saved = session;
        memset(&result, 0xff, sizeof(result));
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_observe_datagram_batch(packet, sizeof(packet),
            local, &session, 32000, &result));
        TEST_ASSERT_INT_EQ(0, result.accepted);
        TEST_ASSERT_INT_EQ(0, result.transition.count);
        TEST_ASSERT_INT_EQ(0, memcmp(&saved, &session, sizeof(saved)));
    }
}
