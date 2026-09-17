#include <string.h>

#include "gvs_station.h"
#include "test.h"

void test_gvs_station_parses_only_door_station_shape(void) {
    uint8_t station[6] = {0};
    const uint8_t expected[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_station_parse("32:02:01:00:02:00", station));
    TEST_ASSERT_INT_EQ(0, memcmp(expected, station, sizeof(station)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_station_parse("IS:2-1-101-1", station));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_station_parse("32:02:01:01:02:00", station));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_station_parse("32:02:01:00:00:00", station));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_station_parse("32:0g:01:00:02:00", station));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_station_parse("32:02:01:00:02:00:01", station));
}

void test_gvs_station_route_requires_fresh_discovery_reply(void) {
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    struct df_gvs_station_routes routes = {0};
    uint32_t ipv4 = 0;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_lookup(
        &routes, station, 100U, 60000U, &ipv4));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_observe(
        &routes, station, 0x01020304U, 100U, false));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_routes_observe(
        &routes, station, 0x01020304U, 100U, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_routes_lookup(
        &routes, station, 60099U, 60000U, &ipv4));
    TEST_ASSERT_INT_EQ(0x01020304, (int)ipv4);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_lookup(
        &routes, station, 60100U, 60000U, &ipv4));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_lookup(
        &routes, station, 99U, 60000U, &ipv4));
}
