#include "gvs_presence.h"
#include "gvs_frame.h"

#include <limits.h>
#include <string.h>

#define DF_GVS_PEER_INITIAL_SECONDS 61U
#define DF_GVS_PEER_RESET_SECONDS 60U
#define DF_GVS_PEER_PROBE_SECONDS 30U
#define DF_GVS_SYNC_START_DELAY_MS 3000U
#define DF_GVS_SYNC_ASK_INTERVAL_MS 500U
#define DF_GVS_SYNC_CHOOSE_INTERVAL_MS 1000U
#define DF_GVS_PERIODIC_SYNC_INTERVAL_MS 60000U
#define DF_GVS_SYNC_ROUNDS 3U

static int df_gvs_presence_emit(enum df_gvs_presence_action_type type,
                                const uint8_t target[6], unsigned round,
                                df_gvs_presence_emit_fn emit, void *context) {
    struct df_gvs_presence_action action = {
        .type = type,
        .round = round,
    };

    if (emit == NULL) {
        return DF_ERR_INVALID;
    }
    if (target != NULL) {
        memcpy(action.target, target, sizeof(action.target));
    }
    return emit(&action, context);
}

static int df_gvs_presence_emit_indoor(
    struct df_gvs_presence *presence, enum df_gvs_presence_action_type type,
    unsigned round, df_gvs_presence_emit_fn emit, void *context) {
    size_t index;

    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; index++) {
        if (presence->peers[index].address[0] == 0x61 &&
            df_gvs_presence_emit(type, presence->peers[index].address, round,
                                 emit, context) != DF_OK) {
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

static int df_gvs_presence_peer_tick(struct df_gvs_presence *presence,
                                     df_gvs_presence_emit_fn emit,
                                     void *context) {
    size_t index;

    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; index++) {
        struct df_gvs_presence_peer *peer = &presence->peers[index];

        if (peer->seconds_remaining > 0U) {
            peer->seconds_remaining--;
        }
        if (peer->seconds_remaining == 0U) {
            peer->online = false;
            peer->seconds_remaining = DF_GVS_PEER_RESET_SECONDS;
            if (df_gvs_presence_emit(DF_GVS_PRESENCE_PEER_OFFLINE,
                                     peer->address, 0, emit,
                                     context) != DF_OK) {
                return DF_ERR_IO;
            }
        } else if (peer->seconds_remaining % DF_GVS_PEER_PROBE_SECONDS == 0U &&
                   df_gvs_presence_emit(DF_GVS_PRESENCE_PEER_PROBE,
                                        peer->address, 0, emit,
                                        context) != DF_OK) {
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

static int df_gvs_presence_phase_tick(struct df_gvs_presence *presence,
                                      df_gvs_presence_emit_fn emit,
                                      void *context) {
    switch (presence->phase) {
    case DF_GVS_PRESENCE_WAIT_SYNC:
        presence->phase = DF_GVS_PRESENCE_SYNC_ASK;
        presence->sync_round = 1U;
        presence->next_phase_ms += DF_GVS_SYNC_ASK_INTERVAL_MS;
        return df_gvs_presence_emit_indoor(
            presence, DF_GVS_PRESENCE_SYNC_ASK_ACTION,
            presence->sync_round, emit, context);
    case DF_GVS_PRESENCE_SYNC_ASK:
        if (presence->sync_round < DF_GVS_SYNC_ROUNDS) {
            presence->sync_round++;
            presence->next_phase_ms += DF_GVS_SYNC_ASK_INTERVAL_MS;
            return df_gvs_presence_emit_indoor(
                presence, DF_GVS_PRESENCE_SYNC_ASK_ACTION,
                presence->sync_round, emit, context);
        }
        presence->phase = DF_GVS_PRESENCE_SYNC_CHOOSE;
        presence->sync_maintainer = true;
        presence->sync_round = 1U;
        presence->next_phase_ms += DF_GVS_SYNC_CHOOSE_INTERVAL_MS;
        return df_gvs_presence_emit_indoor(
            presence, DF_GVS_PRESENCE_SYNC_VERSION_ASK,
            presence->sync_round, emit, context);
    case DF_GVS_PRESENCE_SYNC_CHOOSE:
        if (presence->sync_round < DF_GVS_SYNC_ROUNDS) {
            presence->sync_round++;
            presence->next_phase_ms += DF_GVS_SYNC_CHOOSE_INTERVAL_MS;
            return df_gvs_presence_emit_indoor(
                presence, DF_GVS_PRESENCE_SYNC_VERSION_ASK,
                presence->sync_round, emit, context);
        }
        presence->phase = DF_GVS_PRESENCE_PERIODIC;
        presence->sync_round = 0U;
        presence->next_phase_ms += DF_GVS_PERIODIC_SYNC_INTERVAL_MS;
        return DF_OK;
    case DF_GVS_PRESENCE_PERIODIC:
        presence->next_phase_ms += DF_GVS_PERIODIC_SYNC_INTERVAL_MS;
        if (!presence->sync_maintainer) {
            presence->periodic_misses++;
            if (presence->periodic_misses < 2U) {
                return DF_OK;
            }
            presence->periodic_misses = 0U;
            presence->sync_maintainer = true;
        }
        presence->periodic_misses = 0U;
        return df_gvs_presence_emit_indoor(
            presence, DF_GVS_PRESENCE_PERIODIC_SYNC, 0, emit, context);
    case DF_GVS_PRESENCE_DOWN:
    default:
        return DF_ERR_INVALID;
    }
}

int df_gvs_presence_start(struct df_gvs_presence *presence,
                          const uint8_t identity[6], uint64_t now_ms) {
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6];
    size_t index;

    if (presence == NULL || identity == NULL ||
        df_gvs_identity_indoor_peers(identity, peers) != DF_OK ||
        now_ms > UINT64_MAX - DF_GVS_PERIODIC_SYNC_INTERVAL_MS) {
        return DF_ERR_INVALID;
    }
    memset(presence, 0, sizeof(*presence));
    presence->phase = DF_GVS_PRESENCE_WAIT_SYNC;
    memcpy(presence->identity, identity, sizeof(presence->identity));
    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; index++) {
        memcpy(presence->peers[index].address, peers[index], 6);
        presence->peers[index].seconds_remaining =
            DF_GVS_PEER_INITIAL_SECONDS;
    }
    presence->last_now_ms = now_ms;
    presence->next_peer_tick_ms = now_ms + 1000U;
    presence->next_phase_ms = now_ms + DF_GVS_SYNC_START_DELAY_MS;
    return DF_OK;
}

void df_gvs_presence_stop(struct df_gvs_presence *presence) {
    if (presence != NULL) {
        memset(presence, 0, sizeof(*presence));
    }
}

int df_gvs_presence_tick(struct df_gvs_presence *presence, uint64_t now_ms,
                         df_gvs_presence_emit_fn emit, void *context) {
    if (presence == NULL || emit == NULL ||
        presence->phase == DF_GVS_PRESENCE_DOWN ||
        now_ms < presence->last_now_ms) {
        return DF_ERR_INVALID;
    }
    while (presence->next_peer_tick_ms <= now_ms ||
           presence->next_phase_ms <= now_ms) {
        struct df_gvs_presence next = *presence;

        if (presence->next_peer_tick_ms <= presence->next_phase_ms) {
            if (df_gvs_presence_peer_tick(&next, emit, context) != DF_OK) {
                return DF_ERR_IO;
            }
            next.next_peer_tick_ms += 1000U;
        } else {
            if (df_gvs_presence_phase_tick(&next, emit, context) != DF_OK) {
                return DF_ERR_IO;
            }
        }
        *presence = next;
    }
    presence->last_now_ms = now_ms;
    return DF_OK;
}

int df_gvs_presence_observe_peer(struct df_gvs_presence *presence,
                                 const uint8_t peer[6],
                                 df_gvs_presence_emit_fn emit, void *context) {
    size_t index;

    if (presence == NULL || peer == NULL || emit == NULL ||
        presence->phase == DF_GVS_PRESENCE_DOWN) {
        return DF_ERR_INVALID;
    }
    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; index++) {
        if (memcmp(presence->peers[index].address, peer, 6) == 0) {
            if (df_gvs_presence_emit(DF_GVS_PRESENCE_PEER_ONLINE, peer, 0,
                                     emit, context) != DF_OK) {
                return DF_ERR_IO;
            }
            presence->peers[index].online = true;
            presence->peers[index].seconds_remaining =
                DF_GVS_PEER_RESET_SECONDS;
            return DF_OK;
        }
    }
    return DF_ERR_INVALID;
}

int df_gvs_presence_receive_peer(struct df_gvs_presence *presence,
                                 const uint8_t *data, size_t length,
                                 uint64_t now_ms,
                                 df_gvs_presence_emit_fn emit, void *context) {
    struct df_gvs_frame frame;
    struct df_event event;
    int status;

    /* The six reply bytes are opaque; the source identifies the candidate.
     * Structural acceptance records observation, not authenticated identity.
     * As with sync receive, the caller advances timers before reception. */
    if (presence == NULL || now_ms < presence->last_now_ms ||
        df_gvs_frame_parse(data, length, &frame, &event) != DF_OK ||
        frame.family != 0x07 || frame.opcode != 0x81 ||
        frame.payload_length != 6 ||
        memcmp(frame.destination, presence->identity, 6) != 0) {
        return DF_ERR_INVALID;
    }
    status = df_gvs_presence_observe_peer(presence, frame.source, emit, context);
    if (status == DF_OK) presence->last_now_ms = now_ms;
    return status;
}

void df_gvs_presence_set_sync_maintainer(struct df_gvs_presence *presence,
                                         bool maintainer) {
    if (presence != NULL && presence->phase != DF_GVS_PRESENCE_DOWN) {
        presence->sync_maintainer = maintainer;
        presence->periodic_misses = 0U;
    }
}

int df_gvs_presence_receive_sync(struct df_gvs_presence *presence,
                                 const uint8_t *data, size_t length,
                                 uint64_t now_ms) {
    struct df_gvs_frame frame;
    struct df_event event;
    uint16_t version;

    if (presence == NULL || presence->phase == DF_GVS_PRESENCE_DOWN ||
        now_ms < presence->last_now_ms ||
        now_ms > UINT64_MAX - DF_GVS_PERIODIC_SYNC_INTERVAL_MS ||
        df_gvs_frame_parse(data, length, &frame, &event) != DF_OK ||
        frame.family != 0x91 ||
        (frame.opcode != 0x81 && frame.opcode != 0x82) ||
        frame.payload_length != 2 ||
        memcmp(frame.destination, presence->identity, 6) != 0 ||
        memcmp(frame.source, presence->identity, 5) != 0 ||
        memcmp(frame.source, presence->identity, 6) == 0) {
        return DF_ERR_INVALID;
    }
    version = (uint16_t)(frame.payload[0] | ((uint16_t)frame.payload[1] << 8));
    if (frame.opcode == 0x81 && presence->phase == DF_GVS_PRESENCE_SYNC_ASK) {
        presence->sync_version = 0;
        presence->sync_maintainer = false;
        presence->periodic_misses = 0U;
        presence->sync_round = 0;
        presence->phase = DF_GVS_PRESENCE_PERIODIC;
        presence->next_phase_ms = now_ms + DF_GVS_PERIODIC_SYNC_INTERVAL_MS;
    } else if (frame.opcode == 0x82 &&
               presence->phase == DF_GVS_PRESENCE_SYNC_CHOOSE &&
               version >= presence->sync_version &&
               (int8_t)frame.source[5] < (int8_t)presence->identity[5]) {
        presence->sync_maintainer = false;
    }
    presence->last_now_ms = now_ms;
    return DF_OK;
}

int df_gvs_presence_observe_sync_data(
    struct df_gvs_presence *presence, const uint8_t source[6],
    enum df_gvs_sync_data_type type, uint16_t remote_version,
    uint64_t now_ms, bool *apply_values, bool *resend_local) {
    struct df_gvs_presence next;

    if (apply_values != NULL) {
        *apply_values = false;
    }
    if (resend_local != NULL) {
        *resend_local = false;
    }
    if (presence == NULL || source == NULL || apply_values == NULL ||
        resend_local == NULL || presence->phase != DF_GVS_PRESENCE_PERIODIC ||
        (type != DF_GVS_SYNC_DATA_NORMAL &&
         type != DF_GVS_SYNC_DATA_PERIOD) ||
        now_ms < presence->last_now_ms ||
        now_ms > UINT64_MAX - DF_GVS_PERIODIC_SYNC_INTERVAL_MS ||
        memcmp(source, presence->identity, 5) != 0 ||
        memcmp(source, presence->identity, 6) == 0) {
        return DF_ERR_INVALID;
    }
    next = *presence;
    if (type == DF_GVS_SYNC_DATA_NORMAL) {
        next.sync_maintainer = false;
        next.sync_version = remote_version;
        *apply_values = true;
    } else {
        next.periodic_misses = 0U;
        next.next_phase_ms = now_ms + DF_GVS_PERIODIC_SYNC_INTERVAL_MS;
        if (next.sync_version > remote_version) {
            next.sync_maintainer = true;
            *resend_local = true;
        } else if (next.sync_maintainer &&
                   next.sync_version == remote_version &&
                   (int8_t)next.identity[5] <= (int8_t)source[5]) {
            *resend_local = true;
        } else {
            if (next.sync_maintainer && next.sync_version < remote_version) {
                next.sync_version = remote_version;
            }
            next.sync_maintainer = false;
            *apply_values = true;
        }
    }
    next.last_now_ms = now_ms;
    *presence = next;
    return DF_OK;
}
