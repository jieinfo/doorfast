#include <stdio.h>
#include <string.h>

#include "runtime_config.h"
#include "test.h"

void test_runtime_config_parses_main_gvs_section(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption passive_interface 'vlan-door.42'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption multicast_mode 'auto'\n"
        "\toption multicast_address ''\n"
        "\toption uplink_interface 'br-home'\n"
        "\toption access_material '0d753ea99003cd5d'\n"
        "\toption sync_mini1_secretkey 'mini-one'\n"
        "\toption sync_mini2_secretkey 'mini-two'\n"
        "\toption passive_only '1'\n"
        "\toption capture_promiscuous '0'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.enabled);
    TEST_ASSERT_INT_EQ(0, strcmp("gvs", runtime.config.brand));
    TEST_ASSERT_INT_EQ(0, strcmp("vlan-door.42", runtime.config.gvs_interface));
    TEST_ASSERT_INT_EQ(0, strcmp("IS:2-1-101-1", runtime.config.gvs_local_address));
    TEST_ASSERT_INT_EQ(DF_GVS_MULTICAST_AUTO, runtime.multicast_mode);
    TEST_ASSERT_INT_EQ(0, strcmp("", runtime.multicast_address));
    TEST_ASSERT_INT_EQ(0, strcmp("br-home", runtime.config.uplink_interface));
    TEST_ASSERT_INT_EQ(0, strcmp("0d753ea99003cd5d",
                                 runtime.config.access_material));
    TEST_ASSERT_INT_EQ(0, strcmp("mini-one",
                                 runtime.config.sync_mini1_secretkey));
    TEST_ASSERT_INT_EQ(0, strcmp("mini-two",
                                 runtime.config.sync_mini2_secretkey));
    TEST_ASSERT_INT_EQ(0, strcmp("/etc/config/doorfast-sync",
                                 runtime.config.sync_state_path));
    TEST_ASSERT_INT_EQ(1, runtime.config.passive_only);
    TEST_ASSERT_INT_EQ(0, runtime.config.capture_promiscuous);
    TEST_ASSERT_INT_EQ(0, runtime.config.call_elev);
    TEST_ASSERT_INT_EQ(-1, runtime.config.unlock_delay_seconds);
    TEST_ASSERT_INT_EQ(-1, runtime.config.hangup_delay_seconds);
    TEST_ASSERT_INT_EQ(0, runtime.config.media.publish_retries);
}

void test_runtime_config_selects_a_dedicated_host_interface(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption passive_interface 'br-observe'\n"
        "\toption host_interface 'br-host'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n"
        "\toption indoor_netmask '255.0.0.0'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(0, strcmp("br-host", runtime.config.gvs_interface));
}

void test_runtime_config_accepts_disabled_minimal_config(void) {
    const char input[] = "config gvs 'main'\n\toption enabled '0'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(0, runtime.config.enabled);
    TEST_ASSERT_INT_EQ(1, runtime.config.passive_only);
}

void test_runtime_config_keeps_auto_elevator_inert_in_passive_mode(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption passive_only '1'\n"
        "\toption active_host '0'\n"
        "\toption call_elev '1'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.call_elev);
    TEST_ASSERT_INT_EQ(0, runtime.config.active_host);
}

void test_runtime_config_rejects_ambiguous_or_unsafe_config(void) {
    const char duplicate[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "\toption enabled '1'\n";
    const char active[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption passive_only '0'\n";
    const char invalid_boolean[] =
        "config gvs 'main'\n"
        "\toption enabled 'yes'\n";
    const char relative_state[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption sync_state_path '../doorfast-sync'\n"
        "\toption passive_only '1'\n";
    const char unrelated_state[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption sync_state_path '/etc/passwd'\n"
        "\toption passive_only '1'\n";
    const char malformed_access_material[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption passive_only '1'\n"
        "\toption access_material 'not-hex'\n";
    const char automatic_without_direction[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n"
        "\toption passive_only '0'\n"
        "\toption indoor_netmask '255.0.0.0'\n"
        "\toption call_elev '1'\n";
    const char custom_multicast[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "\toption multicast_mode 'custom'\n"
        "\toption multicast_address '239.1.2.3'\n";
    const char invalid_custom_multicast[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "\toption multicast_mode 'custom'\n"
        "\toption multicast_address '192.168.1.1'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(duplicate, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(active, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(invalid_boolean, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(relative_state, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(unrelated_state, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_config_parse(malformed_access_material,
                                               &runtime));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_runtime_config_parse(automatic_without_direction,
                                               &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.call_elev);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_runtime_config_parse(custom_multicast, &runtime));
    TEST_ASSERT_INT_EQ(DF_GVS_MULTICAST_CUSTOM, runtime.multicast_mode);
    TEST_ASSERT_INT_EQ(0, strcmp("239.1.2.3", runtime.multicast_address));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_config_parse(invalid_custom_multicast, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse("", &runtime));
}

void test_runtime_config_ignores_legacy_elevator_direction(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption passive_only '0'\n"
        "\toption active_host '1'\n"
        "\toption indoor_netmask '255.0.0.0'\n"
        "\toption call_elev '1'\n"
        "\toption call_elev_direction 'down'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.call_elev);
}

void test_runtime_config_derives_active_host_ip_and_requires_netmask(void) {
    const char derived[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'door0'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n"
        "\toption indoor_netmask '255.0.0.0'\n";
    const char manual[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'door0'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n"
        "\toption indoor_ipaddr '10.99.1.7'\n"
        "\toption indoor_netmask '255.255.255.0'\n";
    const char missing_mask[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'door0'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n";
    const char malformed_mask[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'door0'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption active_host '1'\n"
        "\toption indoor_netmask '255.0.255.0'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(derived, &runtime));
    TEST_ASSERT_INT_EQ(0, strcmp(runtime.config.indoor_ipaddr, "10.5.65.0"));
    TEST_ASSERT_INT_EQ(0, strcmp(runtime.config.indoor_netmask, "255.0.0.0"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(manual, &runtime));
    TEST_ASSERT_INT_EQ(0, strcmp(runtime.config.indoor_ipaddr, "10.99.1.7"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_config_parse(missing_mask, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_config_parse(malformed_mask, &runtime));
}

void test_runtime_config_requires_valid_media_prerequisites(void) {
    const char bad[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption active_host '1'\n"
        "\toption host_interface 'eth2'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption indoor_netmask '255.0.0.0'\n"
        "\toption media_enabled '1'\n";
    const char valid[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption active_host '1'\n"
        "\toption host_interface 'eth2'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption indoor_netmask '255.0.0.0'\n"
        "\toption media_enabled '1'\n"
        "\toption media_station_address '32:02:01:00:02:00'\n"
        "\toption media_go2rtc_host 'ha.local'\n"
        "\toption media_relay_url 'https://legacy.invalid/ignored'\n";
    const char secret_in_uci[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "\toption media_ingest_password 'must-not-be-accepted'\n";
    const char secret_outside_main[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "config auxiliary 'main'\n"
        "\toption media_bearer_token 'must-not-be-accepted'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(bad, &runtime));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(valid, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.media.enabled);
    TEST_ASSERT_INT_EQ(8554, runtime.config.media.go2rtc_port);
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_preview", runtime.config.media.stream_name));
    TEST_ASSERT_INT_EQ(1, runtime.stations.count);
    df_runtime_config_destroy(&runtime);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_config_parse(secret_in_uci, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_config_parse(secret_outside_main, &runtime));
}

void test_runtime_config_owns_named_station_registry(void) {
    struct df_runtime_config runtime = {0};
    const struct df_station *station;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_load(
        "tests/fixtures/doorfast-two-stations.conf", &runtime));
    TEST_ASSERT_INT_EQ(DF_GVS_MULTICAST_CUSTOM, runtime.multicast_mode);
    TEST_ASSERT_INT_EQ(0, strcmp("239.1.2.3", runtime.multicast_address));
    TEST_ASSERT_INT_EQ(2, runtime.stations.count);
    station = df_station_registry_find(&runtime.stations, "gate_main");
    TEST_ASSERT_INT_EQ(1, station != NULL);
    df_runtime_config_destroy(&runtime);
}

void test_runtime_config_successful_reload_replaces_owned_registry(void) {
    const char replacement[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "config station 'gate_replacement'\n"
        "\toption enabled '1'\n"
        "\toption name 'Replacement Gate'\n"
        "\toption logical_address '32:02:01:00:04:00'\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_replacement'\n";
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_load(
        "tests/fixtures/doorfast-two-stations.conf", &runtime));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(replacement, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.stations.count);
    TEST_ASSERT_INT_EQ(1, df_station_registry_find(
        &runtime.stations, "gate_replacement") != NULL);
    TEST_ASSERT_INT_EQ(1,
        df_station_registry_find(&runtime.stations, "gate_main") == NULL);
    TEST_ASSERT_INT_EQ(1, runtime.config.brand == runtime.brand);
    TEST_ASSERT_INT_EQ(1,
        runtime.config.gvs_interface == runtime.gvs_interface);
    TEST_ASSERT_INT_EQ(1,
        runtime.config.media.stream_name == runtime.media_stream_name);
    df_runtime_config_destroy(&runtime);
}

void test_runtime_config_failed_reload_preserves_owned_registry(void) {
    const char invalid_replacement[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "config station 'gate_invalid'\n"
        "\toption enabled '1'\n"
        "\toption name 'Invalid Gate'\n"
        "\toption logical_address '32:02:01:00:04:00'\n"
        "\toption stream_name 'doorfast_gate_invalid'\n";
    struct df_runtime_config runtime = {0};
    struct df_station *original_items;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_load(
        "tests/fixtures/doorfast-two-stations.conf", &runtime));
    original_items = runtime.stations.items;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_config_parse(invalid_replacement, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.stations.items == original_items);
    TEST_ASSERT_INT_EQ(2, runtime.stations.count);
    TEST_ASSERT_INT_EQ(DF_GVS_MULTICAST_CUSTOM, runtime.multicast_mode);
    TEST_ASSERT_INT_EQ(0, strcmp("239.1.2.3", runtime.multicast_address));
    TEST_ASSERT_INT_EQ(1,
        df_station_registry_find(&runtime.stations, "gate_main") != NULL);
    df_runtime_config_destroy(&runtime);
}

void test_runtime_config_validates_media_capacity_and_call_policy(void) {
    const char valid[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption active_host '1'\n"
        "\toption host_interface 'eth2'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption indoor_netmask '255.0.0.0'\n"
        "\toption media_enabled '1'\n"
        "\toption media_go2rtc_host 'ha.local'\n"
        "\toption media_max_encoders '2'\n"
        "\toption media_incoming_call_policy 'preempt_oldest_preview'\n"
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_main'\n"
        "config station 'gate_side'\n"
        "\toption enabled '1'\n"
        "\toption name 'Side Gate'\n"
        "\toption logical_address '32:02:01:00:03:00'\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_side'\n";
    const char *capacity = strstr(valid, "media_max_encoders '2'");
    const char *policy = strstr(valid,
        "media_incoming_call_policy 'preempt_oldest_preview'");
    char invalid_capacity[sizeof(valid)];
    char preserve[sizeof(valid)];
    struct df_runtime_config runtime = {0};

    TEST_ASSERT_INT_EQ(1, capacity != NULL);
    TEST_ASSERT_INT_EQ(1, policy != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(valid, &runtime));
    TEST_ASSERT_INT_EQ(2, (int)runtime.config.media.max_encoders);
    TEST_ASSERT_INT_EQ(DF_MEDIA_OVERLOAD_STOP_OLDEST_PREVIEW,
        runtime.config.media.overload_policy);
    df_runtime_config_destroy(&runtime);

    memcpy(invalid_capacity, valid, sizeof(valid));
    invalid_capacity[(size_t)(capacity - valid) +
        strlen("media_max_encoders '")] = '3';
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_config_parse(invalid_capacity, &runtime));

    (void)snprintf(preserve, sizeof(preserve), "%.*s%s%s",
        (int)((policy - valid) +
            strlen("media_incoming_call_policy '")), valid,
        "preserve_previews",
        policy + strlen("media_incoming_call_policy 'preempt_oldest_preview"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(preserve, &runtime));
    TEST_ASSERT_INT_EQ(DF_MEDIA_OVERLOAD_REJECT_NEW,
        runtime.config.media.overload_policy);
    df_runtime_config_destroy(&runtime);
}
