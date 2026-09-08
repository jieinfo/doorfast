#include "gvs_sync_adapters.h"

#include <string.h>

static struct df_gvs_sync_adapter *df_gvs_sync_adapter_find_mutable(
    struct df_gvs_sync_adapter_registry *registry, const char *key) {
    size_t index;

    if (registry == NULL || key == NULL) {
        return NULL;
    }
    for (index = 0; index < registry->count; index++) {
        if (strcmp(registry->adapters[index].key, key) == 0) {
            return &registry->adapters[index];
        }
    }
    return NULL;
}

int df_gvs_sync_adapter_registry_init(
    struct df_gvs_sync_adapter_registry *registry) {
    if (registry == NULL) {
        return DF_ERR_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    registry->adapters[0].key = "sync_mini1_secretkey";
    registry->adapters[0].sensitive = true;
    registry->adapters[1].key = "sync_mini2_secretkey";
    registry->adapters[1].sensitive = true;
    registry->count = DF_GVS_SYNC_FIRST_PARTY_ADAPTERS;
    return DF_OK;
}

const struct df_gvs_sync_adapter *df_gvs_sync_adapter_find(
    const struct df_gvs_sync_adapter_registry *registry, const char *key) {
    size_t index;

    if (registry == NULL || key == NULL) {
        return NULL;
    }
    for (index = 0; index < registry->count; index++) {
        if (strcmp(registry->adapters[index].key, key) == 0) {
            return &registry->adapters[index];
        }
    }
    return NULL;
}

size_t df_gvs_sync_adapter_enabled_count(
    const struct df_gvs_sync_adapter_registry *registry) {
    size_t count = 0;
    size_t index;

    if (registry == NULL) {
        return 0;
    }
    for (index = 0; index < registry->count; index++) {
        if (registry->adapters[index].enabled) {
            count++;
        }
    }
    return count;
}

int df_gvs_sync_adapter_enable(struct df_gvs_sync_adapter_registry *registry,
                               struct df_gvs_sync_store *store,
                               const char *key, const char *initial_value) {
    struct df_gvs_sync_adapter *adapter =
        df_gvs_sync_adapter_find_mutable(registry, key);

    if (adapter == NULL || store == NULL || initial_value == NULL ||
        (adapter->sensitive && initial_value[0] == '\0')) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_sync_store_register(store, key, initial_value) != DF_OK) {
        return DF_ERR_INVALID;
    }
    adapter->enabled = true;
    return DF_OK;
}
