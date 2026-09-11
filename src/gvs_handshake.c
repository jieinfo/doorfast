#include "gvs_handshake.h"

#include <limits.h>
#include <string.h>

#include "event.h"
#include "gvs_frame.h"

static bool df_gvs_handshake_session_active(
    const struct df_gvs_session *session) {
    return session->state == DF_GVS_PREVIEW ||
           session->state == DF_GVS_RINGING ||
           session->state == DF_GVS_TALKING;
}

static bool df_gvs_handshake_nonzero(const uint8_t address[6]) {
    static const uint8_t zero[6] = {0};

    return address != NULL && memcmp(address, zero, sizeof(zero)) != 0;
}

static bool df_gvs_handshake_current(
    const struct df_gvs_handshake *handshake,
    const struct df_gvs_session *session, const uint8_t local[6]) {
    return handshake->active && df_gvs_handshake_session_active(session) &&
           handshake->session_generation == session->generation &&
           memcmp(handshake->peer, session->peer, 6) == 0 &&
           memcmp(handshake->local, local, 6) == 0;
}

static void df_gvs_handshake_stop(struct df_gvs_handshake *handshake) {
    handshake->active = false;
    handshake->next_probe_ms = 0U;
    handshake->missed_replies = 0U;
}

static void df_gvs_handshake_prepare_action(
    const struct df_gvs_handshake *handshake,
    enum df_gvs_handshake_action_type type,
    struct df_gvs_handshake_action *action) {
    action->valid = true;
    action->type = type;
    memcpy(action->destination, handshake->peer, 6);
    memcpy(action->source, handshake->local, 6);
    action->session_generation = handshake->session_generation;
}

int df_gvs_handshake_start(struct df_gvs_handshake *handshake,
                           const struct df_gvs_session *session,
                           const uint8_t local[6], uint64_t now_ms) {
    struct df_gvs_handshake next = {0};

    if (handshake == NULL) {
        return DF_ERR_INVALID;
    }
    memset(handshake, 0, sizeof(*handshake));
    if (session == NULL || !df_gvs_handshake_session_active(session) ||
        session->generation == 0U || !df_gvs_handshake_nonzero(session->peer) ||
        !df_gvs_handshake_nonzero(local) ||
        now_ms > UINT64_MAX - DF_GVS_HANDSHAKE_INTERVAL_MS) {
        return DF_ERR_INVALID;
    }
    next.active = true;
    memcpy(next.peer, session->peer, 6);
    memcpy(next.local, local, 6);
    next.session_generation = session->generation;
    next.next_probe_ms = now_ms;
    next.last_now_ms = now_ms;
    *handshake = next;
    return DF_OK;
}

int df_gvs_handshake_tick(struct df_gvs_handshake *handshake,
                          struct df_gvs_session *session,
                          const uint8_t local[6], uint64_t now_ms,
                          struct df_gvs_handshake_result *result) {
    struct df_gvs_handshake next;
    struct df_gvs_session next_session;
    struct df_gvs_handshake_result next_result = {0};
    bool ended = false;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (handshake == NULL || session == NULL || local == NULL ||
        result == NULL || now_ms < handshake->last_now_ms) {
        return DF_ERR_INVALID;
    }
    next = *handshake;
    next_session = *session;
    next.last_now_ms = now_ms;
    if (!next.active) {
        *handshake = next;
        *result = next_result;
        return DF_OK;
    }
    if (!df_gvs_handshake_current(&next, &next_session, local)) {
        df_gvs_handshake_stop(&next);
        *handshake = next;
        *result = next_result;
        return DF_OK;
    }
    if (now_ms < next.next_probe_ms) {
        *handshake = next;
        *result = next_result;
        return DF_OK;
    }
    if (next.missed_replies >= DF_GVS_HANDSHAKE_MAX_MISSED_REPLIES ||
        now_ms > UINT64_MAX - DF_GVS_HANDSHAKE_INTERVAL_MS) {
        if (df_gvs_session_abort(&next_session, &ended) != DF_OK || !ended) {
            return DF_ERR_INVALID;
        }
        df_gvs_handshake_stop(&next);
        next_result.disconnected = true;
        *handshake = next;
        *session = next_session;
        *result = next_result;
        return DF_OK;
    }
    next.missed_replies++;
    next.next_probe_ms = now_ms + DF_GVS_HANDSHAKE_INTERVAL_MS;
    df_gvs_handshake_prepare_action(&next, DF_GVS_HANDSHAKE_ASK,
                                    &next_result.action);
    *handshake = next;
    *result = next_result;
    return DF_OK;
}

int df_gvs_handshake_receive(struct df_gvs_handshake *handshake,
                             const uint8_t *data, size_t length,
                             const struct df_gvs_session *session,
                             const uint8_t local[6], uint64_t now_ms,
                             struct df_gvs_handshake_result *result) {
    struct df_gvs_handshake next;
    struct df_gvs_handshake_result next_result = {0};
    struct df_gvs_frame frame;
    struct df_event event;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (handshake == NULL || data == NULL || session == NULL ||
        local == NULL || result == NULL || now_ms < handshake->last_now_ms) {
        return DF_ERR_INVALID;
    }
    if (handshake->active &&
        !df_gvs_handshake_current(handshake, session, local)) {
        next = *handshake;
        next.last_now_ms = now_ms;
        df_gvs_handshake_stop(&next);
        *handshake = next;
        return DF_OK;
    }
    if (df_gvs_frame_parse(data, length, &frame, &event) != DF_OK ||
        !handshake->active ||
        frame.family != 0x03 ||
        (frame.opcode != 0x51 && frame.opcode != 0x52) ||
        frame.payload_length != 0U ||
        memcmp(frame.source, handshake->peer, 6) != 0 ||
        memcmp(frame.destination, handshake->local, 6) != 0) {
        return DF_ERR_INVALID;
    }
    next = *handshake;
    next.last_now_ms = now_ms;
    next.missed_replies = 0U;
    if (frame.opcode == 0x51) {
        if (now_ms > UINT64_MAX - DF_GVS_HANDSHAKE_INTERVAL_MS) {
            return DF_ERR_INVALID;
        }
        next.next_probe_ms = now_ms + DF_GVS_HANDSHAKE_INTERVAL_MS;
        next_result.accepted_ask = true;
        df_gvs_handshake_prepare_action(&next, DF_GVS_HANDSHAKE_REPLY,
                                        &next_result.action);
    } else {
        next_result.accepted_reply = true;
    }
    *handshake = next;
    *result = next_result;
    return DF_OK;
}

int df_gvs_handshake_action_serialize(
    const struct df_gvs_handshake_action *action, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context) {
    uint8_t opcode;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (action == NULL || !action->valid ||
        action->session_generation == 0U ||
        !df_gvs_handshake_nonzero(action->destination) ||
        !df_gvs_handshake_nonzero(action->source) ||
        (action->type != DF_GVS_HANDSHAKE_ASK &&
         action->type != DF_GVS_HANDSHAKE_REPLY)) {
        return DF_ERR_INVALID;
    }
    opcode = action->type == DF_GVS_HANDSHAKE_ASK ? 0x51 : 0x52;
    return df_gvs_control_serialize(
        output, capacity, output_length, action->destination, action->source,
        0x03, opcode, NULL, 0U, provide_fields, fields_context);
}
