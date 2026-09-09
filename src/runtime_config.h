#ifndef DOORFAST_RUNTIME_CONFIG_H
#define DOORFAST_RUNTIME_CONFIG_H

#include "config.h"

#define DF_RUNTIME_BRAND_MAX 16
#define DF_RUNTIME_INTERFACE_MAX 64
#define DF_RUNTIME_ADDRESS_MAX 64
#define DF_RUNTIME_PATH_MAX 256

struct df_runtime_config {
    struct df_config config;
    char brand[DF_RUNTIME_BRAND_MAX];
    char gvs_interface[DF_RUNTIME_INTERFACE_MAX];
    char gvs_local_address[DF_RUNTIME_ADDRESS_MAX];
    char uplink_interface[DF_RUNTIME_INTERFACE_MAX];
    char sync_state_path[DF_RUNTIME_PATH_MAX];
};

int df_runtime_config_parse(const char *uci_text, struct df_runtime_config *runtime);
int df_runtime_config_load(const char *path, struct df_runtime_config *runtime);

#endif
