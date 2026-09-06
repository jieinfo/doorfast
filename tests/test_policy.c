#include "policy.h"
#include "session.h"
#include "test.h"

void test_policy_decisions(void) {
    struct df_event event = {.type = DF_EVENT_INCOMING_CALL};
    struct df_policy dnd = {
        .dnd_active = true,
        .schedule_active = true,
        .unlock_delay_seconds = 0,
        .hangup_delay_seconds = 0,
    };
    struct df_policy notify_only = {
        .schedule_active = true,
        .unlock_delay_seconds = -1,
        .hangup_delay_seconds = -1,
    };

    TEST_ASSERT_INT_EQ(DF_DECISION_HANGUP, df_policy_decide(&dnd, &event, 0));
    TEST_ASSERT_INT_EQ(DF_DECISION_NOTIFY, df_policy_decide(&notify_only, &event, 0));
}

void test_session_rejects_different_call_id(void) {
    struct df_session session = {0};
    struct df_event first = {.type = DF_EVENT_INCOMING_CALL, .call_id = "call-a"};
    struct df_event different = {.type = DF_EVENT_INCOMING_CALL, .call_id = "call-b"};

    TEST_ASSERT_INT_EQ(DF_OK, df_session_apply(&session, &first));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_session_apply(&session, &different));
}
