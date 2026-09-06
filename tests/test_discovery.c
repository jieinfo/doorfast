#include <string.h>

#include "discovery.h"
#include "test.h"

void test_discovery_requires_approval(void) {
    struct df_endpoint endpoint = {0};
    struct df_candidate_store store = {0};
    struct df_event event = {
        .type = DF_EVENT_INCOMING_CALL,
        .remote_host = "172.16.1.101",
        .remote_port = 5060,
    };

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_endpoint_parse("10019901:secret@172.16.1.101:5060", &endpoint));
    TEST_ASSERT_INT_EQ(0, strcmp("10019901", endpoint.id));
    TEST_ASSERT_INT_EQ(5060, endpoint.port);
    TEST_ASSERT_INT_EQ(DF_DISCOVERY_CANDIDATE, df_discovery_observe(&event, &store));
    TEST_ASSERT_INT_EQ(0, store.candidates[0].approved);
    TEST_ASSERT_INT_EQ(DF_OK, df_candidate_approve(&store.candidates[0]));
    TEST_ASSERT_INT_EQ(1, store.candidates[0].approved);
    TEST_ASSERT_INT_EQ(DF_DISCOVERY_DUPLICATE, df_discovery_observe(&event, &store));
}
