#ifndef DOORFAST_GVS_PRESENCE_H
#define DOORFAST_GVS_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_identity.h"

enum df_gvs_presence_phase {
    DF_GVS_PRESENCE_DOWN = 0,
    DF_GVS_PRESENCE_WAIT_SYNC,
    DF_GVS_PRESENCE_SYNC_ASK,
    DF_GVS_PRESENCE_SYNC_CHOOSE,
    DF_GVS_PRESENCE_PERIODIC,
};

enum df_gvs_presence_action_type {
    DF_GVS_PRESENCE_PEER_PROBE = 0,
    DF_GVS_PRESENCE_PEER_ONLINE,
    DF_GVS_PRESENCE_PEER_OFFLINE,
    DF_GVS_PRESENCE_SYNC_ASK_ACTION,
    DF_GVS_PRESENCE_SYNC_VERSION_ASK,
    DF_GVS_PRESENCE_PERIODIC_SYNC,
};

enum df_gvs_sync_data_type {
    DF_GVS_SYNC_DATA_NORMAL = 0,
    DF_GVS_SYNC_DATA_PERIOD,
};

struct df_gvs_presence_action {
    enum df_gvs_presence_action_type type;
    uint8_t target[6];
    unsigned round;
};

struct df_gvs_presence_peer {
    uint8_t address[6];
    bool online;
    unsigned seconds_remaining;
};

struct df_gvs_presence {
    enum df_gvs_presence_phase phase;
    uint8_t identity[6];
    struct df_gvs_presence_peer peers[DF_GVS_INDOOR_PEER_COUNT];
    bool sync_maintainer;
    uint16_t sync_version;
    unsigned sync_round;
    unsigned periodic_misses;
    uint64_t last_now_ms;
    uint64_t next_peer_tick_ms;
    uint64_t next_phase_ms;
};

typedef int (*df_gvs_presence_emit_fn)(
    const struct df_gvs_presence_action *action, void *context);

int df_gvs_presence_start(struct df_gvs_presence *presence,
                          const uint8_t identity[6], uint64_t now_ms);
void df_gvs_presence_stop(struct df_gvs_presence *presence);
int df_gvs_presence_tick(struct df_gvs_presence *presence, uint64_t now_ms,
                         df_gvs_presence_emit_fn emit, void *context);
int df_gvs_presence_observe_peer(struct df_gvs_presence *presence,
                                 const uint8_t peer[6],
                                 df_gvs_presence_emit_fn emit, void *context);
int df_gvs_presence_receive_peer(struct df_gvs_presence *presence,
                                 const uint8_t *data, size_t length,
                                 uint64_t now_ms,
                                 df_gvs_presence_emit_fn emit, void *context);
void df_gvs_presence_set_sync_maintainer(struct df_gvs_presence *presence,
                                         bool maintainer);

/* Offline input only. Structural validation is not authentication.
 * Caller supplies monotonic receive time and advances timers separately.
 * Valid replies outside their phase are ignored; invalid inputs never mutate. */
int df_gvs_presence_receive_sync(struct df_gvs_presence *presence,
                                 const uint8_t *data, size_t length,
                                 uint64_t now_ms);
int df_gvs_presence_observe_sync_data(
    struct df_gvs_presence *presence, const uint8_t source[6],
    enum df_gvs_sync_data_type type, uint16_t remote_version,
    uint64_t now_ms, bool *apply_values, bool *resend_local);

#endif
