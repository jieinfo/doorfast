#include <string.h>
#include <stdlib.h>
#include "deployment_health.h"

#include "deployment_snapshot.h"
#include "test.h"

static struct df_deployment_config confirmed_config(void) {
    struct df_deployment_config config;
    const char text[] =
        "config inline 'main'\n"
        " option enabled '1'\n"
        " option bridge 'br-door'\n"
        " option upstream 'door-up'\n"
        " option downstream 'door-down'\n"
        " option management 'br-lan'\n";
    memset(&config, 0, sizeof(config));
    (void)df_deployment_config_parse(text, &config);
    return config;
}

static void set_bridge(struct df_deployment_config *config, const char *bridge) {
    (void)snprintf(config->bridge, sizeof(config->bridge), "%s", bridge);
    (void)snprintf(config->observation, sizeof(config->observation), "%s", bridge);
}

void test_deployment_snapshot_reads_safe_fixture(void) {
    struct df_deployment_config config = confirmed_config();
    struct df_deployment_snapshot snapshot;
    struct df_deployment_report report;

    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(2, (int)snapshot.bridge_member_count);
    TEST_ASSERT_INT_EQ(0, strcmp("door-down", snapshot.bridge_members[0]));
    TEST_ASSERT_INT_EQ(0, strcmp("door-up", snapshot.bridge_members[1]));
    TEST_ASSERT_INT_EQ(1, snapshot.bridge_exists);
    TEST_ASSERT_INT_EQ(1, snapshot.upstream_exists);
    TEST_ASSERT_INT_EQ(1, snapshot.downstream_exists);
    TEST_ASSERT_INT_EQ(1, snapshot.management_exists);
    TEST_ASSERT_INT_EQ(0, snapshot.bridge_has_ipv4);
    TEST_ASSERT_INT_EQ(0, snapshot.bridge_has_ipv6);
    TEST_ASSERT_INT_EQ(0, snapshot.network_interface_reference);
    TEST_ASSERT_INT_EQ(0, snapshot.firewall_reference);
    TEST_ASSERT_INT_EQ(0, snapshot.dhcp_ra_reference);
    TEST_ASSERT_INT_EQ(0, snapshot.stp_enabled);
    TEST_ASSERT_INT_EQ(0, snapshot.multicast_snooping_enabled);
    TEST_ASSERT_INT_EQ(0, snapshot.lldp_active);
    TEST_ASSERT_INT_EQ(1, snapshot.doorfast_passive_only);
    TEST_ASSERT_INT_EQ(1, snapshot.evidence_root_is_mount);
    TEST_ASSERT_INT_EQ(0, snapshot.evidence_root_is_temporary);
    TEST_ASSERT_INT_EQ(1, snapshot.evidence_total_bytes == (UINT64_C(32) << 30));
    TEST_ASSERT_INT_EQ(1, snapshot.evidence_available_bytes == (UINT64_C(30) << 30));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_deployment_preflight_evaluate(&config, &snapshot, &report));
    TEST_ASSERT_INT_EQ(1, report.safe);
    char root[4096];
    struct df_deployment_health health;
    TEST_ASSERT_INT_EQ(1, realpath("tests/fixtures/deployment-root", root) != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_health_collect(&config, root, &health));
    TEST_ASSERT_INT_EQ(1, health.configured);
    TEST_ASSERT_INT_EQ(1, health.upstream.present);
    TEST_ASSERT_INT_EQ(0, health.upstream.carrier_known);
    TEST_ASSERT_INT_EQ(0, health.upstream.counters_known);
    TEST_ASSERT_INT_EQ(0, strcmp(health.upstream.name, "door-up"));
    TEST_ASSERT_INT_EQ(1, health.management.carrier_known);
    TEST_ASSERT_INT_EQ(1, health.management.carrier);
    TEST_ASSERT_INT_EQ(1, health.management.counters_known);
    TEST_ASSERT_INT_EQ(1, health.management.rx_packets == UINT64_C(9007199254740993));
    TEST_ASSERT_INT_EQ(1, health.recorder_present);
    TEST_ASSERT_INT_EQ(0, strcmp(health.recorder_state, "space_guard"));
    TEST_ASSERT_INT_EQ(1, health.reserve_bytes == UINT64_C(6442450944));
}

void test_deployment_snapshot_exposes_unsafe_evidence(void) {
    struct df_deployment_config config = confirmed_config();
    struct df_deployment_snapshot snapshot;

    set_bridge(&config, "br-extra");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(3, (int)snapshot.bridge_member_count);
    set_bridge(&config, "br-firewall");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.firewall_reference);
    set_bridge(&config, "br-dhcp");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.dhcp_ra_reference);
    set_bridge(&config, "br-managed");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.network_interface_reference);
    set_bridge(&config, "br-door");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root-tmpfs", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.evidence_root_is_temporary);
    set_bridge(&config, "br-addressed");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.bridge_has_ipv4);
    TEST_ASSERT_INT_EQ(1, snapshot.bridge_has_ipv6);
}

void test_deployment_snapshot_treats_missing_evidence_as_unsafe(void) {
    struct df_deployment_config config = confirmed_config();
    struct df_deployment_snapshot snapshot;
    struct df_deployment_report report;

    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root-missing", &snapshot));
    TEST_ASSERT_INT_EQ(1, snapshot.stp_enabled);
    TEST_ASSERT_INT_EQ(1, snapshot.multicast_snooping_enabled);
    TEST_ASSERT_INT_EQ(1, snapshot.lldp_active);
    TEST_ASSERT_INT_EQ(0, snapshot.evidence_root_is_mount);
    TEST_ASSERT_INT_EQ(0, snapshot.doorfast_passive_only);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_deployment_preflight_evaluate(&config, &snapshot, &report));
    TEST_ASSERT_INT_EQ(0, report.safe);
    memset(&snapshot, 0x5a, sizeof(snapshot));
    struct df_deployment_snapshot before = snapshot;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_deployment_snapshot_collect(NULL, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &snapshot, sizeof(snapshot)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_deployment_snapshot_collect(&config, "../deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &snapshot, sizeof(snapshot)));
    config = confirmed_config();
    strcpy(config.upstream, "not-ethernet");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &config, "tests/fixtures/deployment-root", &snapshot));
    TEST_ASSERT_INT_EQ(0, snapshot.upstream_exists);
}
