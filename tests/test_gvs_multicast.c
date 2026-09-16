#include <string.h>

#include "gvs_identity.h"
#include "gvs_multicast.h"
#include "test.h"

void test_gvs_multicast_prepares_group_from_local_identity(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_multicast membership = {.fd = -1};

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_identity_parse("IS:2-1-1901-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_multicast_prepare(&membership, identity, "10.5.83.0"));
    TEST_ASSERT_INT_EQ(8300, membership.port);
    TEST_ASSERT_INT_EQ(0,
        strcmp("238.0.203.193", membership.group));
    TEST_ASSERT_INT_EQ(0,
        strcmp("10.5.83.0", membership.local_address));
    TEST_ASSERT_INT_EQ(0, membership.joined);
}
