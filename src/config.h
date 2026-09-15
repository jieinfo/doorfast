#ifndef DOORFAST_CONFIG_H
#define DOORFAST_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "doorfast.h"
#include "gvs_elevator.h"

struct df_config {
    bool enabled;
    const char *brand;
    const char *capture_interface;
    bool capture_auto;
    bool capture_promiscuous;
    const char *gvs_interface;
    const char *gvs_local_address;
    const char *indoor_ipaddr;
    const char *indoor_netmask;
    const char *uplink_interface;
    const char *sync_state_path;
    const char *access_material;
    bool passive_only;
    bool active_host;
    int unlock_delay_seconds;
    int hangup_delay_seconds;
    bool call_elev;
};

#define DF_LEGACY_BRAND_MAX 32

struct df_legacy_import {
    struct df_config config;
    char brand[DF_LEGACY_BRAND_MAX];
};

int df_config_validate(const struct df_config *config);
void df_config_redact(char *dst, size_t dst_size, const char *secret);
int df_config_import_legacy(const char *legacy_uci, struct df_legacy_import *imported);

#endif
