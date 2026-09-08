#include <string.h>

#include "gvs_frame.h"
#include "gvs_identity.h"
#include "test.h"

void test_gvs_identity_parses_and_filters_the_first_five_address_bytes(void) {
    uint8_t local[6] = {0};
    struct df_gvs_frame matching = {.destination = {0x61, 0x02, 0x01, 0x01, 0x01, 0x00}};
    struct df_gvs_frame other_room = {.destination = {0x61, 0x02, 0x01, 0x01, 0x02, 0x00}};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", local));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x61, 0x02, 0x01, 0x01, 0x01, 0x01},
                                 local, sizeof(local)));
    TEST_ASSERT_INT_EQ(1, df_gvs_frame_is_for_identity(&matching, local));
    TEST_ASSERT_INT_EQ(0, df_gvs_frame_is_for_identity(&other_room, local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_identity_parse("IS:2-1-200-0", local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_identity_parse("DOORSTATION:2-1-2-1", local));
}
