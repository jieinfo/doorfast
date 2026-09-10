#include <string.h>
#include "deployment_preflight.h"
#include "test.h"

void test_deployment_preflight_evaluates_snapshot(void) {
    struct df_deployment_config c;
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_config_parse(
        "config inline 'main'\n option enabled '1'\n option bridge 'br-door'\n"
        " option upstream 'door-up'\n option downstream 'door-down'\n"
        " option management 'br-lan'\n", &c));
    struct df_deployment_snapshot good = {
        .bridge_exists = true, .upstream_exists = true,
        .downstream_exists = true, .management_exists = true,
        .bridge_members = {"door-down", "door-up"}, .bridge_member_count = 2,
        .doorfast_passive_only = true, .evidence_root_is_mount = true,
        .evidence_total_bytes = UINT64_C(30) << 30,
        .evidence_available_bytes = UINT64_C(29) << 30
    };
    struct df_deployment_report r;
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_preflight_evaluate(&c, &good, &r));
    TEST_ASSERT_INT_EQ(1, r.safe);
    TEST_ASSERT_INT_EQ(0, r.failures != 0);
    for (unsigned bit = 0; bit < 12; ++bit) {
        struct df_deployment_snapshot s = good;
        switch (bit) {
        case 0: s.management_exists = false; break;
        case 1: s.bridge_member_count = 3; strcpy(s.bridge_members[2], "extra"); break;
        case 2: s.bridge_has_ipv6 = true; break;
        case 3: s.network_interface_reference = true; break;
        case 4: s.firewall_reference = true; break;
        case 5: s.dhcp_ra_reference = true; break;
        case 6: s.stp_enabled = true; break;
        case 7: s.multicast_snooping_enabled = true; break;
        case 8: s.lldp_active = true; break;
        case 9: s.evidence_root_is_temporary = true; break;
        case 10: s.evidence_available_bytes--; break;
        case 11: s.doorfast_passive_only = false; break;
        }
        TEST_ASSERT_INT_EQ(DF_OK, df_deployment_preflight_evaluate(&c, &s, &r));
        TEST_ASSERT_INT_EQ(0, r.safe);
        TEST_ASSERT_INT_EQ(1, r.failures == (UINT64_C(1) << bit));
    }
    struct df_deployment_snapshot bad = {0};
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_preflight_evaluate(&c, &bad, &r));
    TEST_ASSERT_INT_EQ(1, r.failures == (UINT64_C(1) | 2 | 512 | 1024 | 2048));
    bad = good;
    bad.evidence_total_bytes--;
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_preflight_evaluate(&c, &bad, &r));
    TEST_ASSERT_INT_EQ(1, r.failures == 1024);
    bad = good;
    strcpy(bad.bridge_members[0], "door-up");
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_preflight_evaluate(&c, &bad, &r));
    TEST_ASSERT_INT_EQ(1, r.failures == 2);
    struct df_deployment_report before = r;
    bad = good;
    bad.evidence_available_bytes = bad.evidence_total_bytes + 1;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_preflight_evaluate(&c, &bad, &r));
    TEST_ASSERT_INT_EQ(1, r.failures == before.failures && r.safe == before.safe);
    bad = good;
    bad.bridge_member_count = 4;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_preflight_evaluate(&c, &bad, &r));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_preflight_evaluate(NULL, &good, &r));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_preflight_evaluate(&c, NULL, &r));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_preflight_evaluate(&c, &good, NULL));
}
