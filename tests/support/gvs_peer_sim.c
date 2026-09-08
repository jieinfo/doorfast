#include "gvs_peer_sim.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "gvs_identity.h"
#include "gvs_serialize.h"
#include "gvs_sync.h"

struct df_gvs_sim_frame {
    uint8_t data[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length;
};

struct df_gvs_peer_sim {
    enum df_gvs_peer_sim_scenario scenario;
    uint8_t local[6];
    uint8_t lower_peer[6];
    uint8_t door[6];
    uint64_t now_ms;
    struct df_gvs_sim_frame frames[DF_GVS_SIM_FRAME_CAPACITY];
    size_t frame_head;
    size_t frame_count;
    size_t action_counts[DF_GVS_PRESENCE_PERIODIC_SYNC + 1U];
    uint16_t peer_version;
    uint64_t periodic_due_ms;
    bool sync_reply_sent;
    bool periodic_sent;
};

static int df_gvs_sim_header_fields(
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)context;
    memset(random_code, 0x31, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x41, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

static bool df_gvs_peer_sim_target_valid(const struct df_gvs_peer_sim *sim,
                                         const uint8_t target[6]) {
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6];
    size_t index;

    if (sim == NULL || target == NULL ||
        df_gvs_identity_indoor_peers(sim->local, peers) != DF_OK) {
        return false;
    }
    for (index = 0; index < DF_GVS_INDOOR_PEER_COUNT; ++index) {
        if (memcmp(peers[index], target, 6) == 0) {
            return true;
        }
    }
    return false;
}

static int df_gvs_peer_sim_enqueue_control(
    struct df_gvs_peer_sim *sim, const uint8_t destination[6],
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, uint16_t payload_length) {
    uint8_t data[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length;
    size_t index;

    if (sim == NULL || sim->frame_count >= DF_GVS_SIM_FRAME_CAPACITY) {
        return DF_ERR_IO;
    }
    if (df_gvs_control_serialize(
            data, sizeof(data), &length, destination, source, family, opcode,
            payload, payload_length, df_gvs_sim_header_fields, NULL) != DF_OK) {
        return DF_ERR_INVALID;
    }
    index = (sim->frame_head + sim->frame_count) %
            DF_GVS_SIM_FRAME_CAPACITY;
    memcpy(sim->frames[index].data, data, length);
    sim->frames[index].length = length;
    sim->frame_count++;
    return DF_OK;
}

static int df_gvs_peer_sim_enqueue_frame(struct df_gvs_peer_sim *sim,
                                         const uint8_t *data,
                                         size_t length) {
    size_t index;

    if (sim == NULL || data == NULL || length == 0U ||
        length > DF_GVS_SYNC_MAX_PACKET_SIZE ||
        sim->frame_count >= DF_GVS_SIM_FRAME_CAPACITY) {
        return DF_ERR_IO;
    }
    index = (sim->frame_head + sim->frame_count) %
            DF_GVS_SIM_FRAME_CAPACITY;
    memcpy(sim->frames[index].data, data, length);
    sim->frames[index].length = length;
    sim->frame_count++;
    return DF_OK;
}

int df_gvs_peer_sim_create(struct df_gvs_peer_sim **sim,
                           enum df_gvs_peer_sim_scenario scenario,
                           const uint8_t local[6], uint64_t now_ms) {
    struct df_gvs_peer_sim *created;
    char address[DF_GVS_IPV4_TEXT_SIZE];

    if (sim == NULL) {
        return DF_ERR_INVALID;
    }
    *sim = NULL;
    if (local == NULL || scenario < DF_GVS_SIM_NO_PEER ||
        scenario > DF_GVS_SIM_MAINTAINER_LOSS ||
        df_gvs_identity_unicast_ip(local, address) != DF_OK) {
        return DF_ERR_INVALID;
    }
    created = calloc(1, sizeof(*created));
    if (created == NULL) {
        return DF_ERR_IO;
    }
    created->scenario = scenario;
    memcpy(created->local, local, sizeof(created->local));
    memcpy(created->lower_peer, local, sizeof(created->lower_peer));
    created->lower_peer[5] = 0x01;
    created->door[0] = 0x32;
    created->door[1] = local[1];
    created->door[2] = local[2];
    created->door[3] = 0x00;
    created->door[4] = local[4];
    created->door[5] = 0x00;
    created->now_ms = now_ms;
    created->peer_version = 7U;
    *sim = created;
    return DF_OK;
}

void df_gvs_peer_sim_destroy(struct df_gvs_peer_sim *sim) {
    free(sim);
}

int df_gvs_peer_sim_emit(const struct df_gvs_presence_action *action,
                         void *context) {
    struct df_gvs_peer_sim *sim = context;
    struct df_gvs_peer_sim next;
    uint8_t payload[2];
    bool version_reply;
    bool sync_reply;

    if (sim == NULL || action == NULL ||
        action->type < DF_GVS_PRESENCE_PEER_PROBE ||
        action->type > DF_GVS_PRESENCE_PERIODIC_SYNC ||
        !df_gvs_peer_sim_target_valid(sim, action->target)) {
        return DF_ERR_INVALID;
    }
    version_reply = sim->scenario == DF_GVS_SIM_LOWER_PEER &&
                    action->type == DF_GVS_PRESENCE_SYNC_VERSION_ASK &&
                    !sim->sync_reply_sent &&
                    memcmp(action->target, sim->lower_peer, 6) == 0;
    sync_reply = sim->scenario == DF_GVS_SIM_MAINTAINER_LOSS &&
                 action->type == DF_GVS_PRESENCE_SYNC_ASK_ACTION &&
                 !sim->sync_reply_sent &&
                 memcmp(action->target, sim->lower_peer, 6) == 0;
    if (!version_reply && !sync_reply) {
        sim->action_counts[action->type]++;
        return DF_OK;
    }
    if (sync_reply && sim->now_ms > UINT64_MAX - 60000U) {
        return DF_ERR_INVALID;
    }
    next = *sim;
    payload[0] = (uint8_t)(next.peer_version & 0xffU);
    payload[1] = (uint8_t)(next.peer_version >> 8U);
    if (df_gvs_peer_sim_enqueue_control(
            &next, next.local, next.lower_peer, 0x91,
            version_reply ? 0x82 : 0x81, payload, sizeof(payload)) != DF_OK) {
        return DF_ERR_IO;
    }
    next.sync_reply_sent = true;
    if (sync_reply) {
        next.periodic_due_ms = next.now_ms + 60000U;
    }
    next.action_counts[action->type]++;
    *sim = next;
    return DF_OK;
}

int df_gvs_peer_sim_advance(struct df_gvs_peer_sim *sim, uint64_t now_ms) {
    struct df_gvs_peer_sim next;
    struct df_gvs_sync_store remote_store;
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PERIODIC_SYNC,
    };
    uint8_t data[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length;

    if (sim == NULL || now_ms < sim->now_ms) {
        return DF_ERR_INVALID;
    }
    next = *sim;
    next.now_ms = now_ms;
    if (next.scenario == DF_GVS_SIM_MAINTAINER_LOSS &&
        next.sync_reply_sent && !next.periodic_sent &&
        now_ms >= next.periodic_due_ms) {
        df_gvs_sync_store_init(&remote_store);
        if (df_gvs_sync_store_register(&remote_store, "sim_state", "present") !=
            DF_OK) {
            return DF_ERR_INVALID;
        }
        memcpy(action.target, next.local, sizeof(action.target));
        if (df_gvs_sync_periodic_serialize(
                &remote_store, 0, &action, next.lower_peer,
                next.peer_version, data, sizeof(data), &length,
                df_gvs_sim_header_fields, NULL) != DF_OK ||
            df_gvs_peer_sim_enqueue_frame(&next, data, length) != DF_OK) {
            return DF_ERR_IO;
        }
        next.periodic_sent = true;
    }
    *sim = next;
    return DF_OK;
}

int df_gvs_peer_sim_next_frame(struct df_gvs_peer_sim *sim,
                               const uint8_t **frame, size_t *length) {
    if (frame != NULL) {
        *frame = NULL;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (sim == NULL || frame == NULL || length == NULL) {
        return DF_ERR_INVALID;
    }
    if (sim->frame_count == 0U) {
        return DF_ERR_IO;
    }
    *frame = sim->frames[sim->frame_head].data;
    *length = sim->frames[sim->frame_head].length;
    sim->frame_head = (sim->frame_head + 1U) % DF_GVS_SIM_FRAME_CAPACITY;
    sim->frame_count--;
    return DF_OK;
}

int df_gvs_peer_sim_make_call(struct df_gvs_peer_sim *sim,
                              const uint8_t destination[6]) {
    if (sim == NULL || destination == NULL) {
        return DF_ERR_INVALID;
    }
    return df_gvs_peer_sim_enqueue_control(sim, destination, sim->door, 0x03,
                                           0x01, NULL, 0);
}

size_t df_gvs_peer_sim_action_count(
    const struct df_gvs_peer_sim *sim,
    enum df_gvs_presence_action_type type) {
    if (sim == NULL || type < DF_GVS_PRESENCE_PEER_PROBE ||
        type > DF_GVS_PRESENCE_PERIODIC_SYNC) {
        return 0U;
    }
    return sim->action_counts[type];
}
