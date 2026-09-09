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

struct call_binding_test {
    struct df_gvs_call_control_status status;
    struct df_runtime_call_request request;
    uint64_t submitted_at;
    unsigned status_calls;
    unsigned submit_calls;
};

static int provide_call_status(
    struct df_gvs_call_control_status *status, void *context) {
    struct call_binding_test *test = context;
    *status = test->status;
    test->status_calls++;
    return DF_OK;
}

static int submit_call(const struct df_runtime_call_request *request,
                       uint64_t now_ms, void *context) {
    struct call_binding_test *test = context;
    test->request = *request;
    test->submitted_at = now_ms;
    test->submit_calls++;
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

void test_runtime_ubus_validates_and_routes_call_requests(void) {
    struct df_runtime_ubus service = {0};
    struct call_binding_test test = {0};
    struct df_gvs_call_control_status status;
    struct df_runtime_call_request answer = {
        .type = DF_GVS_CALL_COMMAND_ANSWER,
        .session_generation = 9,
        .primary_media_port = 8303,
        .secondary_media_port = 8302,
        .duration_seconds = 120,
    };
    struct df_runtime_call_request hangup = {
        .type = DF_GVS_CALL_COMMAND_HANGUP,
        .session_generation = 9,
        .reason = 1,
    };
    unsigned sync_calls = 0;

    test.status.session_state = DF_GVS_RINGING;
    test.status.session_generation = 9;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &test));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &test));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_call_status(&service, &status));
    TEST_ASSERT_INT_EQ(9, status.session_generation);
    TEST_ASSERT_INT_EQ(1, test.status_calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 11));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &answer));
    TEST_ASSERT_INT_EQ(11, test.submitted_at);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, test.request.type);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &hangup));
    TEST_ASSERT_INT_EQ(2, test.submit_calls);
    answer.primary_media_port = 0;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &answer));
    hangup.duration_seconds = 1;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &hangup));
    TEST_ASSERT_INT_EQ(2, test.submit_calls);
    df_runtime_ubus_stop(&service);
}
