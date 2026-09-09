#ifndef DOORFAST_GVS_SYNC_ADAPTERS_H
#define DOORFAST_GVS_SYNC_ADAPTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "gvs_sync.h"

#define DF_GVS_SYNC_FIRST_PARTY_ADAPTERS 2U

struct df_gvs_sync_adapter {
    const char *key;
    bool sensitive;
    bool enabled;
};

struct df_gvs_sync_adapter_registry {
    struct df_gvs_sync_adapter adapters[DF_GVS_SYNC_FIRST_PARTY_ADAPTERS];
    size_t count;
};

int df_gvs_sync_adapter_registry_init(
    struct df_gvs_sync_adapter_registry *registry);
const struct df_gvs_sync_adapter *df_gvs_sync_adapter_find(
    const struct df_gvs_sync_adapter_registry *registry, const char *key);
size_t df_gvs_sync_adapter_enabled_count(
    const struct df_gvs_sync_adapter_registry *registry);
int df_gvs_sync_adapter_enable(struct df_gvs_sync_adapter_registry *registry,
                               struct df_gvs_sync_store *store,
                               const char *key, const char *initial_value);

#endif
