#include "gvs_elevator_query.h"
#include "test.h"

#include <string.h>

struct elevator_query_sender {
    int results[3];
    unsigned calls;
    struct df_gvs_elevator_request last;
};

static int elevator_query_send(
    const struct df_gvs_elevator_request *request, void *context) {
    struct elevator_query_sender *sender = context;
    if (request == NULL || sender == NULL || sender->calls >= 3U)
        return DF_ERR_INVALID;
    sender->last = *request;
    return sender->results[sender->calls++];
}

void test_gvs_elevator_query_sends_immediately_and_periodically(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_query_sender sender = {{DF_OK, DF_ERR_IO, DF_OK}, 0, {0}};
    struct df_gvs_elevator_query query;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_init(
        &query, local, true, 0, elevator_query_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_tick(&query, 0));
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
    TEST_ASSERT_INT_EQ(0x03, sender.last.opcode);
    TEST_ASSERT_INT_EQ(1, (int)query.successful_sends);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_tick(&query, 999));
    TEST_ASSERT_INT_EQ(1, (int)sender.calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_tick(&query, 1000));
    TEST_ASSERT_INT_EQ(2, (int)sender.calls);
    TEST_ASSERT_INT_EQ(1, (int)query.failed_sends);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_tick(&query, 2000));
    TEST_ASSERT_INT_EQ(2, (int)query.successful_sends);
}

void test_gvs_elevator_query_disabled_and_clock_safe(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct elevator_query_sender sender = {{DF_OK, DF_OK, DF_OK}, 0, {0}};
    struct df_gvs_elevator_query query;
    struct df_gvs_elevator_query snapshot;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_init(
        &query, local, false, 10, elevator_query_send, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_query_tick(&query, 10000));
    TEST_ASSERT_INT_EQ(0, (int)sender.calls);
    snapshot = query;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_elevator_query_tick(&query, 9));
    TEST_ASSERT_INT_EQ(0, memcmp(&snapshot, &query, sizeof(query)));
}
