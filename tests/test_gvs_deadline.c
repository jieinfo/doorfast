#include <string.h>

#include "gvs_deadline.h"
#include "test.h"

void test_gvs_deadline_expires_without_another_packet(void) {
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .generation = 4,
        .pick_generation = 4,
    };
    struct df_gvs_deadline deadline = {0};
    bool timed_out = false;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 1000, 30000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_tick(&deadline, &session, 30999, &timed_out));
    TEST_ASSERT_INT_EQ(0, timed_out);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_tick(&deadline, &session, 31000, &timed_out));
    TEST_ASSERT_INT_EQ(1, timed_out);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, session.pick_generation);
}

void test_gvs_deadline_does_not_end_a_new_generation(void) {
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 2};
    struct df_gvs_deadline deadline = {0};
    bool timed_out = false;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 50, 100));
    session.generation = 3;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_tick(&deadline, &session, 200, &timed_out));
    TEST_ASSERT_INT_EQ(0, timed_out);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
}

void test_gvs_deadline_rejects_backwards_time(void) {
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 7};
    struct df_gvs_deadline deadline = {0};
    bool timed_out = false;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 500, 100));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_deadline_tick(&deadline, &session, 499, &timed_out));
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
}
