#include "diagnostics.h"
#include "test.h"

void test_network_overlap_is_read_only(void) {
    struct df_network_info doorfast = {.address = "192.168.5.1", .prefix_length = 24};
    struct df_network_info entry = {.address = "192.168.5.20", .prefix_length = 24};
    struct df_network_info separate = {.address = "172.16.1.20", .prefix_length = 24};

    TEST_ASSERT_INT_EQ(DF_OVERLAP_WARNING, df_diagnostics_overlap(&doorfast, &entry));
    TEST_ASSERT_INT_EQ(DF_OVERLAP_NONE, df_diagnostics_overlap(&doorfast, &separate));
}
