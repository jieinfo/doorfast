#include <string.h>
#include <stdint.h>

#include "gvs_deadline.h"
#include "test.h"

void test_gvs_deadline_remaining_unarmed_is_zero(void) {
    struct df_gvs_deadline deadline = {0};
    uint64_t remaining = 99;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_remaining(&deadline, 10, &remaining));
    TEST_ASSERT_INT_EQ(0, remaining);
}

void test_gvs_deadline_remaining_reports_relative_time_without_mutation(void) {
    struct df_gvs_deadline deadline = {
        .armed = true,
        .generation = 3,
        .started_ms = 1000,
        .timeout_ms = 30000,
    };
    struct df_gvs_deadline before = deadline;
    uint64_t remaining = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_remaining(&deadline, 12500, &remaining));
    TEST_ASSERT_INT_EQ(18500, remaining);
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &deadline, sizeof(deadline)));
}

void test_gvs_deadline_remaining_handles_expiry_and_zero_timeout(void) {
    struct df_gvs_deadline deadline = {
        .armed = true,
        .started_ms = 100,
        .timeout_ms = 50,
    };
    uint64_t remaining = 99;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_remaining(&deadline, 150, &remaining));
    TEST_ASSERT_INT_EQ(0, remaining);
    deadline.timeout_ms = 0;
    remaining = 99;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_remaining(&deadline, 100, &remaining));
    TEST_ASSERT_INT_EQ(0, remaining);
}

void test_gvs_deadline_remaining_avoids_uint64_overflow(void) {
    struct df_gvs_deadline deadline = {
        .armed = true,
        .started_ms = UINT64_MAX - 3,
        .timeout_ms = UINT64_MAX,
    };
    uint64_t remaining = 0;

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_deadline_remaining(&deadline, UINT64_MAX - 1, &remaining));
    TEST_ASSERT_INT_EQ(1, remaining == UINT64_MAX - 2);
}

void test_gvs_deadline_remaining_rejects_backwards_time(void) {
    struct df_gvs_deadline deadline = {
        .armed = true,
        .started_ms = 500,
        .timeout_ms = 100,
    };
    uint64_t remaining = 77;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_deadline_remaining(&deadline, 499, &remaining));
    TEST_ASSERT_INT_EQ(0, remaining);
}

void test_gvs_deadline_remaining_rejects_invalid_pointers_and_clears_output(void) {
    struct df_gvs_deadline deadline = {0};
    uint64_t remaining = 77;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_deadline_remaining(NULL, 0, &remaining));
    TEST_ASSERT_INT_EQ(0, remaining);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_deadline_remaining(&deadline, 0, NULL));
}

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
