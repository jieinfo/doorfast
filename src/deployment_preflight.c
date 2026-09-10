#include "deployment_preflight.h"
#include <string.h>

int df_deployment_preflight_evaluate(const struct df_deployment_config *c,
    const struct df_deployment_snapshot *s, struct df_deployment_report *out) {
    if (!s || !out || df_deployment_config_validate(c) != DF_OK ||
        s->bridge_member_count > DF_ARRAY_LEN(s->bridge_members) ||
        s->evidence_available_bytes > s->evidence_total_bytes) return DF_ERR_INVALID;
    for (size_t i = 0; i < s->bridge_member_count; ++i)
        if (!s->bridge_members[i][0] || strnlen(s->bridge_members[i],
            DF_DEPLOYMENT_IFNAME_MAX) == DF_DEPLOYMENT_IFNAME_MAX) return DF_ERR_INVALID;
    uint64_t f = 0;
    if (!s->bridge_exists || !s->upstream_exists || !s->downstream_exists ||
        !s->management_exists) f |= DF_DEPLOYMENT_MISSING_INTERFACE;
    if (s->bridge_member_count != 2 ||
        !((!strcmp(s->bridge_members[0], c->upstream) &&
           !strcmp(s->bridge_members[1], c->downstream)) ||
          (!strcmp(s->bridge_members[1], c->upstream) &&
           !strcmp(s->bridge_members[0], c->downstream))))
        f |= DF_DEPLOYMENT_BAD_BRIDGE_MEMBERS;
    if (s->bridge_has_ipv4 || s->bridge_has_ipv6) f |= DF_DEPLOYMENT_BRIDGE_HAS_ADDRESS;
    if (s->network_interface_reference) f |= DF_DEPLOYMENT_BRIDGE_MANAGED;
    if (s->firewall_reference) f |= DF_DEPLOYMENT_FIREWALL_REFERENCE;
    if (s->dhcp_ra_reference) f |= DF_DEPLOYMENT_DHCP_RA_REFERENCE;
    if (s->stp_enabled) f |= DF_DEPLOYMENT_STP_ENABLED;
    if (s->multicast_snooping_enabled) f |= DF_DEPLOYMENT_MULTICAST_SNOOPING;
    if (s->lldp_active) f |= DF_DEPLOYMENT_LLDP_ACTIVE;
    if (!s->evidence_root_is_mount || s->evidence_root_is_temporary) f |= DF_DEPLOYMENT_BAD_MOUNT;
    uint64_t budget = (uint64_t)c->recent_budget_mib + c->control_budget_mib +
        c->log_budget_mib + c->reserve_mib;
    if (s->evidence_total_bytes < (UINT64_C(30) << 30) ||
        s->evidence_available_bytes < (UINT64_C(29) << 30) ||
        s->evidence_available_bytes < (budget << 20)) f |= DF_DEPLOYMENT_LOW_CAPACITY;
    if (!s->doorfast_passive_only) f |= DF_DEPLOYMENT_NOT_PASSIVE;
    *out = (struct df_deployment_report){.failures = f, .safe = f == 0};
    return DF_OK;
}
