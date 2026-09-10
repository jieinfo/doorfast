#ifndef DF_DEPLOYMENT_HEALTH_H
#define DF_DEPLOYMENT_HEALTH_H
#include "deployment_config.h"
struct df_link_health {
    bool present, carrier_known, carrier, counters_known;
    char name[DF_DEPLOYMENT_IFNAME_MAX];
    uint64_t rx_packets, tx_packets, rx_dropped, tx_dropped, rx_errors, tx_errors;
};
struct df_deployment_health {
    bool configured, preflight_safe, passive_only;
    struct df_link_health upstream, downstream, management;
    char observation_interface[DF_DEPLOYMENT_IFNAME_MAX];
    bool recorder_present;
    char recorder_state[24];
    uint64_t recent_bytes, control_bytes, available_bytes, reserve_bytes;
};
int df_deployment_health_load(struct df_deployment_health *);
int df_deployment_health_collect(const struct df_deployment_config *,
    const char *root, struct df_deployment_health *);
#endif
