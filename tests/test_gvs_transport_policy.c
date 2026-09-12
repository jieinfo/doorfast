#include "test.h"
#include "gvs_transport_policy.h"

static void test_policy_defaults_to_safe(void) {
    const struct df_gvs_transport_policy policy = {
        .passive_only = true,
        .real_send_requested = false,
        .one_shot = false,
        .rollback_ready = false,
    };
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_transport_policy_validate(&policy));
}

static void test_policy_rejects_real_send_in_passive_mode(void) {
    const struct df_gvs_transport_policy policy = {
        .passive_only = true,
        .real_send_requested = true,
        .one_shot = true,
        .rollback_ready = true,
    };
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                      df_gvs_transport_policy_validate(&policy));
}

static void test_policy_requires_one_shot_and_rollback(void) {
    struct df_gvs_transport_policy policy = {
        .passive_only = false,
        .real_send_requested = true,
        .one_shot = false,
        .rollback_ready = true,
    };
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                      df_gvs_transport_policy_validate(&policy));
    policy.one_shot = true;
    policy.rollback_ready = false;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                      df_gvs_transport_policy_validate(&policy));
    policy.rollback_ready = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_transport_policy_validate(&policy));
}

static void test_policy_rejects_null(void) {
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                      df_gvs_transport_policy_validate(NULL));
}

void test_gvs_transport_policy(void) {
    test_policy_defaults_to_safe();
    test_policy_rejects_real_send_in_passive_mode();
    test_policy_requires_one_shot_and_rollback();
    test_policy_rejects_null();
}
