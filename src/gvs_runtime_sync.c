#include "gvs_runtime_sync.h"

#include <stdio.h>
#include <string.h>

#include "gvs_frame.h"

int df_gvs_runtime_sync_start(struct df_gvs_runtime_sync *sync,
                              const uint8_t identity[6], uint16_t version,
                              uint64_t now_ms) {
    if (sync == NULL || version > 60000U) {
        return DF_ERR_INVALID;
    }
    memset(sync, 0, sizeof(*sync));
    df_gvs_sync_store_init(&sync->store);
    if (df_gvs_sync_adapter_registry_init(&sync->adapters) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_presence_start(&sync->presence, identity, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    sync->presence.sync_version = version;
    return DF_OK;
}

int df_gvs_runtime_sync_restart(struct df_gvs_runtime_sync *sync,
                                uint64_t now_ms) {
    uint8_t identity[6];
    uint16_t version;

    if (sync == NULL) {
        return DF_ERR_INVALID;
    }
    memcpy(identity, sync->presence.identity, sizeof(identity));
    version = sync->presence.sync_version;
    if (df_gvs_presence_start(&sync->presence, identity, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    sync->presence.sync_version = version;
    return DF_OK;
}

void df_gvs_runtime_sync_stop(struct df_gvs_runtime_sync *sync) {
    if (sync != NULL) {
        uint8_t identity[6];
        uint16_t version = sync->presence.sync_version;

        memcpy(identity, sync->presence.identity, sizeof(identity));
        df_gvs_presence_stop(&sync->presence);
        memcpy(sync->presence.identity, identity, sizeof(identity));
        sync->presence.sync_version = version;
    }
}

int df_gvs_runtime_sync_tick(struct df_gvs_runtime_sync *sync,
                             uint64_t now_ms, df_gvs_presence_emit_fn emit,
                             void *context) {
    if (sync == NULL) {
        return DF_ERR_INVALID;
    }
    return df_gvs_presence_tick(&sync->presence, now_ms, emit, context);
}

int df_gvs_runtime_sync_receive(
    struct df_gvs_runtime_sync *sync, const uint8_t *data, size_t length,
    uint64_t now_ms, struct df_gvs_runtime_sync_result *result) {
    struct df_gvs_frame frame;
    struct df_event event;
    uint16_t previous_version;
    bool previous_maintainer;
    int status = DF_ERR_INVALID;
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5,
    };

    if (sync == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    memset(result, 0, sizeof(*result));
    if (data == NULL || length < 42U || memcmp(data, magic, sizeof(magic)) != 0 ||
        data[38] != 0x91) {
        return DF_OK;
    }
    result->handled = true;
    result->opcode = data[39];
    if (df_gvs_frame_parse(data, length, &frame, &event) != DF_OK) {
        result->rejected = true;
        sync->last_receive = *result;
        return DF_OK;
    }
    previous_version = sync->presence.sync_version;
    previous_maintainer = sync->presence.sync_maintainer;
    if (frame.opcode == 0x81 || frame.opcode == 0x82) {
        status = df_gvs_presence_receive_sync(&sync->presence, data, length,
                                              now_ms);
    } else if (frame.opcode == 0x03) {
        status = df_gvs_sync_receive(&sync->store, &sync->presence, data,
                                     length, now_ms, &result->resend_local);
    } else if (frame.opcode == 0x01 || frame.opcode == 0x02) {
        status = DF_OK;
    }
    if (status != DF_OK) {
        result->rejected = true;
        sync->last_receive = *result;
        return DF_OK;
    }
    result->accepted = true;
    result->version_changed = previous_version != sync->presence.sync_version;
    result->maintainer_changed =
        previous_maintainer != sync->presence.sync_maintainer;
    sync->last_receive = *result;
    return DF_OK;
}

static enum df_gvs_sync_role df_gvs_runtime_sync_role(
    const struct df_gvs_runtime_sync *sync) {
    if (sync->presence.phase == DF_GVS_PRESENCE_DOWN) {
        return DF_GVS_SYNC_ROLE_DOWN;
    }
    if (sync->presence.phase != DF_GVS_PRESENCE_PERIODIC) {
        return DF_GVS_SYNC_ROLE_STARTING;
    }
    return sync->presence.sync_maintainer ? DF_GVS_SYNC_ROLE_MAINTAINER
                                         : DF_GVS_SYNC_ROLE_FOLLOWER;
}

int df_gvs_runtime_sync_status(
    const struct df_gvs_runtime_sync *sync,
    struct df_gvs_runtime_sync_status *status) {
    size_t index;

    if (sync == NULL || status == NULL ||
        sync->presence.phase > DF_GVS_PRESENCE_PERIODIC) {
        return DF_ERR_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->phase = sync->presence.phase;
    status->role = df_gvs_runtime_sync_role(sync);
    status->sync_version = sync->presence.sync_version;
    status->periodic_misses = sync->presence.periodic_misses;
    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; index++) {
        if (sync->presence.peers[index].online) {
            status->online_peers++;
        }
    }
    status->registered_adapters = sync->adapters.count;
    status->enabled_adapters =
        df_gvs_sync_adapter_enabled_count(&sync->adapters);
    status->last_receive = sync->last_receive;
    return DF_OK;
}

static const char *df_gvs_runtime_sync_phase_name(
    enum df_gvs_presence_phase phase) {
    switch (phase) {
    case DF_GVS_PRESENCE_DOWN: return "down";
    case DF_GVS_PRESENCE_WAIT_SYNC: return "wait_sync";
    case DF_GVS_PRESENCE_SYNC_ASK: return "sync_ask";
    case DF_GVS_PRESENCE_SYNC_CHOOSE: return "sync_choose";
    case DF_GVS_PRESENCE_PERIODIC: return "periodic";
    default: return "unknown";
    }
}

static const char *df_gvs_runtime_sync_role_name(enum df_gvs_sync_role role) {
    switch (role) {
    case DF_GVS_SYNC_ROLE_DOWN: return "down";
    case DF_GVS_SYNC_ROLE_STARTING: return "starting";
    case DF_GVS_SYNC_ROLE_MAINTAINER: return "maintainer";
    case DF_GVS_SYNC_ROLE_FOLLOWER: return "follower";
    default: return "unknown";
    }
}

static const char *df_gvs_runtime_sync_bool(bool value) {
    return value ? "true" : "false";
}

int df_gvs_runtime_sync_status_json(const struct df_gvs_runtime_sync *sync,
                                    char *output, size_t capacity) {
    struct df_gvs_runtime_sync_status status;
    int length;

    if (output == NULL || capacity == 0U) {
        return DF_ERR_INVALID;
    }
    output[0] = '\0';
    if (df_gvs_runtime_sync_status(sync, &status) != DF_OK) {
        return DF_ERR_INVALID;
    }
    length = snprintf(
        output, capacity,
        "{\"phase\":\"%s\",\"role\":\"%s\",\"sync_version\":%u,"
        "\"periodic_misses\":%u,\"online_peers\":%zu,"
        "\"adapters\":{\"registered\":%zu,\"enabled\":%zu},"
        "\"last_receive\":{\"opcode\":%u,\"handled\":%s,"
        "\"accepted\":%s,\"rejected\":%s,\"resend_local\":%s}}",
        df_gvs_runtime_sync_phase_name(status.phase),
        df_gvs_runtime_sync_role_name(status.role),
        (unsigned)status.sync_version, status.periodic_misses,
        status.online_peers, status.registered_adapters,
        status.enabled_adapters, (unsigned)status.last_receive.opcode,
        df_gvs_runtime_sync_bool(status.last_receive.handled),
        df_gvs_runtime_sync_bool(status.last_receive.accepted),
        df_gvs_runtime_sync_bool(status.last_receive.rejected),
        df_gvs_runtime_sync_bool(status.last_receive.resend_local));
    if (length < 0 || (size_t)length >= capacity) {
        output[0] = '\0';
        return DF_ERR_INVALID;
    }
    return DF_OK;
}
