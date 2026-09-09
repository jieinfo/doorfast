#ifndef DOORFAST_GVS_RUNTIME_SYNC_H
#define DOORFAST_GVS_RUNTIME_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_presence.h"
#include "gvs_sync.h"
#include "gvs_sync_adapters.h"

enum df_gvs_sync_role {
    DF_GVS_SYNC_ROLE_DOWN = 0,
    DF_GVS_SYNC_ROLE_STARTING,
    DF_GVS_SYNC_ROLE_MAINTAINER,
    DF_GVS_SYNC_ROLE_FOLLOWER,
};

struct df_gvs_runtime_sync_result {
    bool handled;
    bool accepted;
    bool rejected;
    bool resend_local;
    bool version_changed;
    bool maintainer_changed;
    uint8_t opcode;
};

struct df_gvs_runtime_sync {
    struct df_gvs_presence presence;
    struct df_gvs_sync_store store;
    struct df_gvs_sync_adapter_registry adapters;
    struct df_gvs_runtime_sync_result last_receive;
};

struct df_gvs_runtime_sync_status {
    enum df_gvs_presence_phase phase;
    enum df_gvs_sync_role role;
    uint16_t sync_version;
    unsigned periodic_misses;
    size_t online_peers;
    size_t registered_adapters;
    size_t enabled_adapters;
    struct df_gvs_runtime_sync_result last_receive;
};

int df_gvs_runtime_sync_start(struct df_gvs_runtime_sync *sync,
                              const uint8_t identity[6], uint16_t version,
                              uint64_t now_ms);
int df_gvs_runtime_sync_restart(struct df_gvs_runtime_sync *sync,
                                uint64_t now_ms);
void df_gvs_runtime_sync_stop(struct df_gvs_runtime_sync *sync);
int df_gvs_runtime_sync_tick(struct df_gvs_runtime_sync *sync,
                             uint64_t now_ms, df_gvs_presence_emit_fn emit,
                             void *context);
int df_gvs_runtime_sync_receive(
    struct df_gvs_runtime_sync *sync, const uint8_t *data, size_t length,
    uint64_t now_ms, struct df_gvs_runtime_sync_result *result);
int df_gvs_runtime_sync_status(
    const struct df_gvs_runtime_sync *sync,
    struct df_gvs_runtime_sync_status *status);
const char *df_gvs_runtime_sync_phase_name(
    enum df_gvs_presence_phase phase);
const char *df_gvs_runtime_sync_role_name(enum df_gvs_sync_role role);
int df_gvs_runtime_sync_status_json(const struct df_gvs_runtime_sync *sync,
                                    char *output, size_t capacity);

#endif
