#ifndef DOORFAST_GVS_HANDSHAKE_H
#define DOORFAST_GVS_HANDSHAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_serialize.h"
#include "gvs_session.h"

#define DF_GVS_HANDSHAKE_INTERVAL_MS 2000U
#define DF_GVS_HANDSHAKE_MAX_MISSED_REPLIES 5U
#define DF_GVS_HANDSHAKE_FRAME_SIZE DF_GVS_CONTROL_HEADER_SIZE

enum df_gvs_handshake_action_type {
    DF_GVS_HANDSHAKE_NONE = 0,
    DF_GVS_HANDSHAKE_ASK,
    DF_GVS_HANDSHAKE_REPLY,
};

struct df_gvs_handshake_action {
    bool valid;
    enum df_gvs_handshake_action_type type;
    uint8_t destination[6];
    uint8_t source[6];
    uint64_t session_generation;
};

struct df_gvs_handshake {
    bool active;
    uint8_t peer[6];
    uint8_t local[6];
    uint64_t session_generation;
    uint64_t next_probe_ms;
    uint64_t last_now_ms;
    unsigned missed_replies;
};

struct df_gvs_handshake_result {
    struct df_gvs_handshake_action action;
    bool accepted_ask;
    bool accepted_reply;
    bool disconnected;
};

/* Offline state only. Start schedules the first 03/51 immediately. */
int df_gvs_handshake_start(struct df_gvs_handshake *handshake,
                           const struct df_gvs_session *session,
                           const uint8_t local[6], uint64_t now_ms);
int df_gvs_handshake_tick(struct df_gvs_handshake *handshake,
                          struct df_gvs_session *session,
                          const uint8_t local[6], uint64_t now_ms,
                          struct df_gvs_handshake_result *result);
int df_gvs_handshake_receive(struct df_gvs_handshake *handshake,
                             const uint8_t *data, size_t length,
                             const struct df_gvs_session *session,
                             const uint8_t local[6], uint64_t now_ms,
                             struct df_gvs_handshake_result *result);
int df_gvs_handshake_action_serialize(
    const struct df_gvs_handshake_action *action, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

#endif
