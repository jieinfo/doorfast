#ifndef DF_DEPLOYMENT_CONFIG_H
#define DF_DEPLOYMENT_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
#include "doorfast.h"
#define DF_DEPLOYMENT_IFNAME_MAX 64
#define DF_DEPLOYMENT_PATH_MAX 256
struct df_deployment_config {
    bool enabled, recording_enabled;
    char bridge[DF_DEPLOYMENT_IFNAME_MAX];
    char upstream[DF_DEPLOYMENT_IFNAME_MAX];
    char downstream[DF_DEPLOYMENT_IFNAME_MAX];
    char management[DF_DEPLOYMENT_IFNAME_MAX];
    char evidence_root[DF_DEPLOYMENT_PATH_MAX];
    uint32_t recent_budget_mib, control_budget_mib, log_budget_mib, reserve_mib;
};
int df_deployment_config_parse(const char *, struct df_deployment_config *);
int df_deployment_config_validate(const struct df_deployment_config *);
#endif
