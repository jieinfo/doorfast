#include <string.h>

#include "event.h"
#include "gvs_frame.h"
#include "gvs_session.h"
#include "test.h"

void test_gvs_session_preemption_transaction(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t next[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t payload[7] = {0};
    struct df_gvs_session session = {.state = DF_GVS_RINGING,
        .peer = {0x61, 2, 1, 1, 1, 2}, .generation = 8,
        .pick_generation = 8, .pick_started_ms = 500,
        .pick_timeout_ms = 3000, .pick_local = {0x61, 2, 1, 1, 1, 1}};
    struct df_gvs_session before;
    struct df_gvs_preemption result;
    struct df_event hangup = {.type = DF_EVENT_HANGUP};
    struct df_gvs_frame old_frame = {.source = {0x61, 2, 1, 1, 1, 2}};
    struct df_gvs_frame old_reply = {.source = {0x61, 2, 1, 1, 1, 2},
        .destination = {0x61, 2, 1, 1, 1, 1}, .family = 3, .opcode = 0x83,
        .payload = payload, .payload_length = 7};
    before = session;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_preempt(&session, 8, next, 4, 0, 1000, &result));
    TEST_ASSERT_INT_EQ(2, result.count);
    TEST_ASSERT_INT_EQ(DF_EVENT_HANGUP, result.events[0].type);
    TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, result.events[1].type);
    TEST_ASSERT_INT_EQ(0, memcmp(result.events[0].peer, before.peer, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(result.events[1].peer, next, 6));
    TEST_ASSERT_INT_EQ(1, result.events[0].generation == 8 && result.events[1].generation == 9);
    TEST_ASSERT_INT_EQ(1, result.ring_started_ms == 1000 && session.generation == 9);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, next, 6));
    TEST_ASSERT_INT_EQ(1, session.pick_generation == 0 && session.pick_started_ms == 0 &&
        session.pick_timeout_ms == 0);
    TEST_ASSERT_INT_EQ(0, memcmp(session.pick_local, (uint8_t[6]){0}, 6));
    before = session;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_apply(&session, &old_frame, &hangup));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_observe_pick(&session, &old_reply, local, 1100, 3000));
    memcpy(old_frame.source, next, 6);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_apply_for_generation(&session, 8, &old_frame, &hangup));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &session, sizeof(session)));

    /* A fresh exchange with the replacement peer still works. */
    struct df_gvs_frame new_request = {.family = 3, .opcode = 3,
        .payload = payload, .payload_length = 7};
    struct df_gvs_frame new_reply = old_reply;
    memcpy(new_request.source, local, 6);
    memcpy(new_request.destination, next, 6);
    memcpy(new_reply.source, next, 6);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_session_observe_pick(&session, &new_reply, local, 1200, 3000));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_session_observe_pick(&session, &new_request, local, 1200, 3000));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_session_observe_pick(&session, &old_reply, local, 1250, 3000));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_session_observe_pick(&session, &new_reply, local, 1300, 3000));
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);

    /* All rejection paths preserve state and clear stale output. */
    for (int scenario = 0; scenario < 8; ++scenario) {
        session = before;
        int current = 0, incoming = 4;
        uint64_t expected = session.generation;
        const uint8_t *peer = old_reply.source;
        if (scenario == 1) incoming = -1;
        if (scenario == 2) { current = 4; incoming = 0; expected--; }
        if (scenario == 3) { current = 4; incoming = 0; peer = next; }
        if (scenario == 4) { current = 4; incoming = 0; session.state = DF_GVS_TALKING; }
        if (scenario == 5) { current = 4; incoming = 0; session.state = DF_GVS_PREVIEW; }
        if (scenario == 6) { current = 4; incoming = 0; expected = session.generation = UINT64_MAX; }
        if (scenario == 7) { current = 4; incoming = 0; peer = NULL; }
        struct df_gvs_session saved = session;
        memset(&result, 0xff, sizeof(result));
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_preempt(&session, expected,
            peer, current, incoming, 1200, &result));
        TEST_ASSERT_INT_EQ(0, result.count);
        TEST_ASSERT_INT_EQ(0, memcmp(&saved, &session, sizeof(session)));
    }
}

void test_gvs_session_tracks_passive_lifecycle(void) {
    struct df_gvs_session session = {0};
    struct df_gvs_frame frame = {.source = {0x32, 0, 0, 0, 0, 2}};
    struct df_event preview = {.type = DF_EVENT_PREVIEW_STARTED};
    struct df_event talking = {.type = DF_EVENT_SESSION_ESTABLISHED};
    struct df_event hangup = {.type = DF_EVENT_HANGUP};
    struct df_event pick_reply = {.type = DF_EVENT_PICK_REPLY_OBSERVED};
    struct df_gvs_frame different_peer = {.source = {0x32, 0, 0, 0, 0, 3}};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &preview));
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_apply(&session, &frame, &pick_reply));
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, frame.source, sizeof(session.peer)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &talking));
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_session_apply(&session, &different_peer, &hangup));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &hangup));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
}

void test_gvs_session_allows_a_new_call_after_end_and_rejects_old_peer(void) {
    struct df_gvs_session session = {0};
    struct df_gvs_frame first_peer = {.source = {0x32, 0, 0, 0, 0, 2}};
    struct df_gvs_frame next_peer = {.source = {0x32, 0, 0, 0, 0, 3}};
    struct df_event incoming = {.type = DF_EVENT_INCOMING_CALL};
    struct df_event hangup = {.type = DF_EVENT_HANGUP};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &first_peer, &incoming));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &first_peer, &hangup));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &next_peer, &incoming));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, next_peer.source, sizeof(session.peer)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_session_apply(&session, &first_peer, &hangup));
}

void test_gvs_session_rejects_callbacks_from_an_older_generation(void) {
    struct df_gvs_session session = {0};
    struct df_gvs_frame first_peer = {.source = {0x32, 0, 0, 0, 0, 2}};
    struct df_gvs_frame next_peer = {.source = {0x32, 0, 0, 0, 0, 3}};
    struct df_event incoming = {.type = DF_EVENT_INCOMING_CALL};
    struct df_event hangup = {.type = DF_EVENT_HANGUP};
    uint64_t first_generation;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &first_peer, &incoming));
    first_generation = session.generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &first_peer, &hangup));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &next_peer, &incoming));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_session_apply_for_generation(&session, first_generation,
                                                           &first_peer, &hangup));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
}

void test_gvs_pick_exchange(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t payload[7] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
    struct df_gvs_frame request = {.family = 3, .opcode = 3,
        .source = {0x61, 2, 1, 1, 1, 1}, .destination = {0x32, 2, 1, 0, 1, 0},
        .payload = payload, .payload_length = 7};
    struct df_gvs_frame reply = {.family = 3, .opcode = 0x83,
        .destination = {0x61, 2, 1, 1, 1, 1}, .source = {0x32, 2, 1, 0, 1, 0},
        .payload = payload, .payload_length = 7};
    struct df_gvs_session session = {.state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0}, .generation = 1};
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_observe_pick(&session, &reply, local, 100, 3000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_observe_pick(&session, &request, local, 100, 3000));
    reply.destination[5] = 2;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_observe_pick(&session, &reply, local, 200, 3000));
    reply.destination[5] = 1;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_observe_pick(&session, &reply, local, 300, 3000));
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
    session.state = DF_GVS_RINGING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_observe_pick(&session, &request, local, 1000, 3000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_observe_pick(&session, &request, local, 3900, 3000));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_observe_pick(&session, &reply, local, 4000, 3000));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_observe_pick(&session, &request, local, 5000, 3000));
    session.generation++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_session_observe_pick(&session, &reply, local, 5100, 3000));
}

void test_gvs_session_abort_clears_active_exchange(void) {
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 4,
        .pick_generation = 4,
        .pick_started_ms = 100,
        .pick_timeout_ms = 3000,
        .pick_local = {0x61, 2, 1, 1, 1, 1},
    };
    bool ended = false;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_abort(&session, &ended));
    TEST_ASSERT_INT_EQ(1, ended);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, (int)session.pick_generation);
    TEST_ASSERT_INT_EQ(0, (int)session.pick_started_ms);
    TEST_ASSERT_INT_EQ(0, (int)session.pick_timeout_ms);
    TEST_ASSERT_INT_EQ(0, memcmp(session.pick_local, (uint8_t[6]){0}, 6));

    ended = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_abort(&session, &ended));
    TEST_ASSERT_INT_EQ(0, ended);
}
