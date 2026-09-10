#ifndef DF_DEPLOYMENT_PREFLIGHT_H
#define DF_DEPLOYMENT_PREFLIGHT_H
#include <stddef.h>
#include "deployment_config.h"
enum df_deployment_failure {
    DF_DEPLOYMENT_MISSING_INTERFACE = 1ULL << 0,
    DF_DEPLOYMENT_BAD_BRIDGE_MEMBERS = 1ULL << 1,
    DF_DEPLOYMENT_BRIDGE_HAS_ADDRESS = 1ULL << 2,
    DF_DEPLOYMENT_BRIDGE_MANAGED = 1ULL << 3,
    DF_DEPLOYMENT_FIREWALL_REFERENCE = 1ULL << 4,
    DF_DEPLOYMENT_DHCP_RA_REFERENCE = 1ULL << 5,
    DF_DEPLOYMENT_STP_ENABLED = 1ULL << 6,
    DF_DEPLOYMENT_MULTICAST_SNOOPING = 1ULL << 7,
    DF_DEPLOYMENT_LLDP_ACTIVE = 1ULL << 8,
    DF_DEPLOYMENT_BAD_MOUNT = 1ULL << 9,
    DF_DEPLOYMENT_LOW_CAPACITY = 1ULL << 10,
    DF_DEPLOYMENT_NOT_PASSIVE = 1ULL << 11
};
struct df_deployment_snapshot {
    bool bridge_exists, upstream_exists, downstream_exists, management_exists;
    char bridge_members[3][DF_DEPLOYMENT_IFNAME_MAX];
    size_t bridge_member_count;
    bool bridge_has_ipv4, bridge_has_ipv6;
    bool network_interface_reference, firewall_reference, dhcp_ra_reference;
    bool stp_enabled, multicast_snooping_enabled, lldp_active;
    bool doorfast_passive_only;
    bool evidence_root_is_mount, evidence_root_is_temporary;
    uint64_t evidence_total_bytes, evidence_available_bytes;
};
struct df_deployment_report { uint64_t failures; bool safe; };
int df_deployment_preflight_evaluate(const struct df_deployment_config *,
    const struct df_deployment_snapshot *, struct df_deployment_report *);
#endif
