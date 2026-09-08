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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_identity_parse("IS:2-1-133-1", local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_identity_parse("DOORSTATION:2-1-2-1", local));
}

void test_gvs_identity_derives_network_addresses_and_indoor_peers(void) {
    uint8_t local[6] = {0};
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6] = {{0}};
    char unicast[DF_GVS_IPV4_TEXT_SIZE] = {0};
    char multicast[DF_GVS_IPV4_TEXT_SIZE] = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", local));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_unicast_ip(local, unicast));
    TEST_ASSERT_INT_EQ(0, strcmp("10.5.65.0", unicast));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_multicast_ip(local, multicast));
    TEST_ASSERT_INT_EQ(0, strcmp("238.0.201.129", multicast));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_indoor_peers(local, peers));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x61, 0x02, 0x01, 0x01, 0x01, 0x02},
                                 peers[0], 6));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x61, 0x02, 0x01, 0x01, 0x01, 0x03},
                                 peers[1], 6));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x61, 0x02, 0x01, 0x01, 0x01, 0x04},
                                 peers[2], 6));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x62, 0x02, 0x01, 0x01, 0x01, 0x01},
                                 peers[3], 6));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x62, 0x02, 0x01, 0x01, 0x01, 0x02},
                                 peers[4], 6));
}
