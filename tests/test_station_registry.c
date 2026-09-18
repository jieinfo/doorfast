#include <arpa/inet.h>
#include <string.h>

#include "station_registry.h"
#include "test.h"

void test_station_registry_loads_named_sections_and_owns_memory(void) {
    struct df_station_registry registry = {0};
    const struct df_station *station;
    struct in_addr expected_ipv4;
    size_t iteration;

    for (iteration = 0U; iteration < 3U; iteration++) {
        TEST_ASSERT_INT_EQ(DF_OK, df_station_registry_load(
            &registry, "tests/fixtures/doorfast-two-stations.conf"));
        TEST_ASSERT_INT_EQ(2, registry.count);
        TEST_ASSERT_INT_EQ(1, registry.revision);
        station = df_station_registry_find(&registry, "gate_main");
        TEST_ASSERT_INT_EQ(1, station != NULL);
        if (station != NULL) {
            TEST_ASSERT_INT_EQ(0, strcmp("Main Gate", station->name));
            TEST_ASSERT_INT_EQ(0, strcmp("doorfast_gate_main",
                                         station->stream_name));
            TEST_ASSERT_INT_EQ(DF_STATION_ROUTE_DISCOVER_FIRST,
                               station->route_preference);
            TEST_ASSERT_INT_EQ(1, station->enabled);
        }
        station = df_station_registry_find(&registry, "gate_service");
        TEST_ASSERT_INT_EQ(1, station != NULL);
        TEST_ASSERT_INT_EQ(1, inet_pton(AF_INET, "10.2.1.30", &expected_ipv4));
        if (station != NULL) {
            TEST_ASSERT_INT_EQ((int)expected_ipv4.s_addr,
                               (int)station->configured_ipv4);
            TEST_ASSERT_INT_EQ(DF_STATION_ROUTE_FIXED,
                               station->route_preference);
            TEST_ASSERT_INT_EQ(0, station->enabled);
        }
        TEST_ASSERT_INT_EQ(1,
            df_station_registry_find(&registry, "missing") == NULL);
        df_station_registry_destroy(&registry);
        TEST_ASSERT_INT_EQ(0, registry.count);
        TEST_ASSERT_INT_EQ(1, registry.items == NULL);
    }
    df_station_registry_destroy(&registry);
}

void test_station_registry_rejects_duplicate_identity_fields(void) {
    const char duplicate_id[] =
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption stream_name 'doorfast_gate_main'\n"
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Other Gate'\n"
        "\toption logical_address '32:02:01:00:03:00'\n"
        "\toption stream_name 'doorfast_gate_other'\n";
    const char duplicate_address[] =
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption stream_name 'doorfast_gate_main'\n"
        "config station 'gate_other'\n"
        "\toption enabled '1'\n"
        "\toption name 'Other Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption stream_name 'doorfast_gate_other'\n";
    const char duplicate_stream[] =
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption stream_name 'doorfast_gate'\n"
        "config station 'gate_other'\n"
        "\toption enabled '1'\n"
        "\toption name 'Other Gate'\n"
        "\toption logical_address '32:02:01:00:03:00'\n"
        "\toption stream_name 'doorfast_gate'\n";
    struct df_station_registry registry = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_station_registry_parse(&registry, duplicate_id));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_station_registry_parse(&registry, duplicate_address));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_station_registry_parse(&registry, duplicate_stream));
    TEST_ASSERT_INT_EQ(0, registry.count);
    df_station_registry_destroy(&registry);
}

void test_station_registry_requires_ipv4_for_fixed_routes(void) {
    const char input[] =
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'fixed'\n"
        "\toption stream_name 'doorfast_gate_main'\n";
    struct df_station_registry registry = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_station_registry_parse(&registry, input));
    TEST_ASSERT_INT_EQ(0, registry.count);
    df_station_registry_destroy(&registry);
}

void test_station_registry_maps_legacy_singleton_options(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption media_station_address '32:02:01:00:02:00'\n"
        "\toption media_station_ipv4 '10.2.1.20'\n"
        "\toption media_stream_name 'doorfast_preview'\n";
    const uint8_t expected_address[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    struct df_station_registry registry = {0};
    const struct df_station *station;

    TEST_ASSERT_INT_EQ(DF_OK, df_station_registry_parse(&registry, input));
    TEST_ASSERT_INT_EQ(1, registry.count);
    station = df_station_registry_find(&registry, "legacy");
    TEST_ASSERT_INT_EQ(1, station != NULL);
    if (station != NULL) {
        TEST_ASSERT_INT_EQ(0, memcmp(expected_address,
                                     station->logical_address,
                                     sizeof(expected_address)));
        TEST_ASSERT_INT_EQ(0, strcmp("doorfast_preview",
                                     station->stream_name));
        TEST_ASSERT_INT_EQ(DF_STATION_ROUTE_DISCOVER_FIRST,
                           station->route_preference);
        TEST_ASSERT_INT_EQ(1, station->enabled);
    }
    df_station_registry_destroy(&registry);
}
