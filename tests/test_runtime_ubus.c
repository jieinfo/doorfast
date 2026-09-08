#include <string.h>

#include "runtime_ubus.h"
#include "test.h"

static int provide_runtime_status(
    struct df_gvs_runtime_sync_status *status, void *context) {
    unsigned *calls = context;

    if (status == NULL || calls == NULL) {
        return DF_ERR_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->phase = DF_GVS_PRESENCE_PERIODIC;
    status->role = DF_GVS_SYNC_ROLE_FOLLOWER;
    status->sync_version = 23;
    (*calls)++;
    return DF_OK;
}

void test_runtime_ubus_stub_validates_lifecycle_without_side_effects(void) {
    struct df_runtime_ubus service = {0};
    unsigned calls = 0;

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(NULL, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_runtime_ubus_start(&service, NULL, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 11));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 9));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_stop(&service);
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 12));
}
