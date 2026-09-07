#include <string.h>

#include "event.h"
#include "gvs_frame.h"
#include "gvs_session.h"
#include "test.h"

void test_gvs_session_tracks_passive_lifecycle(void) {
    struct df_gvs_session session = {0};
    struct df_gvs_frame frame = {.source = {0x32, 0, 0, 0, 0, 2}};
    struct df_event preview = {.type = DF_EVENT_PREVIEW_STARTED};
    struct df_event talking = {.type = DF_EVENT_SESSION_ESTABLISHED};
    struct df_event hangup = {.type = DF_EVENT_HANGUP};
    struct df_gvs_frame different_peer = {.source = {0x32, 0, 0, 0, 0, 3}};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &preview));
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, frame.source, sizeof(session.peer)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &talking));
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_session_apply(&session, &different_peer, &hangup));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &hangup));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
}
