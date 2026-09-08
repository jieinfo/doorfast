#include "gvs_session.h"

#include <string.h>
#include "gvs_priority.h"

int df_gvs_session_preempt(struct df_gvs_session *session,
    uint64_t expected_generation, const uint8_t incoming_peer[6],
    int current_category, int incoming_category, uint64_t now_ms,
    struct df_gvs_preemption *result) {
    if (result == NULL) return DF_ERR_INVALID;
    memset(result, 0, sizeof(*result));
    if (session == NULL || incoming_peer == NULL ||
        session->state != DF_GVS_RINGING || expected_generation == 0 ||
        expected_generation != session->generation || session->generation == UINT64_MAX ||
        memcmp(session->peer, incoming_peer, 6) == 0 ||
        df_gvs_priority_compare(current_category, incoming_category) != DF_GVS_PRIORITY_PREEMPT) {
        return DF_ERR_INVALID;
    }
    result->events[0].type = DF_EVENT_HANGUP;
    memcpy(result->events[0].peer, session->peer, 6);
    result->events[0].generation = session->generation;
    result->events[1].type = DF_EVENT_INCOMING_CALL;
    memcpy(result->events[1].peer, incoming_peer, 6);
    result->events[1].generation = session->generation + 1;
    result->ring_started_ms = now_ms;
    memcpy(session->peer, result->events[1].peer, 6);
    session->generation++;
    session->pick_generation = 0;
    session->pick_started_ms = 0;
    session->pick_timeout_ms = 0;
    memset(session->pick_local, 0, sizeof(session->pick_local));
    result->count = 2;
    return DF_OK;
}

static int df_gvs_session_is_active(const struct df_gvs_session *session) {
    return session->state == DF_GVS_PREVIEW || session->state == DF_GVS_RINGING ||
           session->state == DF_GVS_TALKING;
}

static void df_gvs_session_set_peer(struct df_gvs_session *session,
                                    const struct df_gvs_frame *frame) {
    memcpy(session->peer, frame->source, sizeof(session->peer));
}

static void df_gvs_session_start(struct df_gvs_session *session,
                                 const struct df_gvs_frame *frame,
                                 enum df_gvs_session_state state) {
    session->generation++;
    session->pick_generation = 0;
    if (session->generation == 0) {
        session->generation = 1;
    }
    df_gvs_session_set_peer(session, frame);
    session->state = state;
}

int df_gvs_session_apply(struct df_gvs_session *session, const struct df_gvs_frame *frame,
                         const struct df_event *event) {
    if (session == NULL || frame == NULL || event == NULL) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_session_is_active(session) &&
        memcmp(session->peer, frame->source, sizeof(session->peer)) != 0) {
        return DF_ERR_INVALID;
    }

    if ((session->state == DF_GVS_IDLE || session->state == DF_GVS_ENDED) &&
        event->type == DF_EVENT_PREVIEW_STARTED) {
        df_gvs_session_start(session, frame, DF_GVS_PREVIEW);
        return DF_OK;
    }
    if ((session->state == DF_GVS_IDLE || session->state == DF_GVS_ENDED) &&
        event->type == DF_EVENT_INCOMING_CALL) {
        df_gvs_session_start(session, frame, DF_GVS_RINGING);
        return DF_OK;
    }
    if ((session->state == DF_GVS_PREVIEW || session->state == DF_GVS_RINGING) &&
        event->type == DF_EVENT_SESSION_ESTABLISHED) {
        session->state = DF_GVS_TALKING;
        return DF_OK;
    }
    if (df_gvs_session_is_active(session) && event->type == DF_EVENT_HANGUP) {
        session->state = DF_GVS_ENDED;
        session->pick_generation = 0;
        return DF_OK;
    }
    if (session->state == DF_GVS_RINGING && event->type == DF_EVENT_INCOMING_CALL) {
        return DF_OK; /* Same-peer retransmission; preserve the current generation. */
    }
    if (df_gvs_session_is_active(session) && event->type == DF_EVENT_UNLOCK_RESULT_OBSERVED) {
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

int df_gvs_session_apply_for_generation(struct df_gvs_session *session, uint64_t generation,
                                        const struct df_gvs_frame *frame,
                                        const struct df_event *event) {
    if (session == NULL || generation == 0 || generation != session->generation) {
        return DF_ERR_INVALID;
    }
    return df_gvs_session_apply(session, frame, event);
}

int df_gvs_session_abort(struct df_gvs_session *session, bool *ended) {
    if (session == NULL || ended == NULL) {
        return DF_ERR_INVALID;
    }
    *ended = false;
    if (!df_gvs_session_is_active(session)) {
        return DF_OK;
    }
    session->state = DF_GVS_ENDED;
    session->pick_generation = 0;
    session->pick_started_ms = 0;
    session->pick_timeout_ms = 0;
    memset(session->pick_local, 0, sizeof(session->pick_local));
    *ended = true;
    return DF_OK;
}

int df_gvs_session_observe_pick(struct df_gvs_session *session,
    const struct df_gvs_frame *frame, const uint8_t local[6],
    uint64_t now_ms, uint64_t timeout_ms) {
    if (session == NULL || frame == NULL || local == NULL || timeout_ms == 0 ||
        session->generation == 0 ||
        (session->state != DF_GVS_RINGING && session->state != DF_GVS_PREVIEW) ||
        frame->family != 3 || frame->payload_length != 7 || frame->payload == NULL) {
        return DF_ERR_INVALID;
    }
    if (frame->opcode == 3) {
        if (memcmp(frame->source, local, 6) != 0 ||
            memcmp(frame->destination, session->peer, 6) != 0) {
            return DF_ERR_INVALID;
        }
        if (session->pick_generation == session->generation) {
            /* Retransmissions must not extend the original observation window. */
            return now_ms >= session->pick_started_ms &&
                now_ms - session->pick_started_ms < session->pick_timeout_ms
                ? DF_OK : DF_ERR_INVALID;
        }
        session->pick_generation = session->generation;
        session->pick_started_ms = now_ms;
        session->pick_timeout_ms = timeout_ms;
        memcpy(session->pick_local, local, 6);
        return DF_OK;
    }
    if (frame->opcode != 0x83 ||
        memcmp(frame->source, session->peer, 6) != 0 ||
        memcmp(frame->destination, local, 6) != 0 ||
        memcmp(session->pick_local, local, 6) != 0 ||
        session->pick_generation != session->generation) {
        return DF_ERR_INVALID;
    }
    if (now_ms < session->pick_started_ms ||
        now_ms - session->pick_started_ms >= session->pick_timeout_ms) {
        session->pick_generation = 0;
        return DF_ERR_INVALID;
    }
    session->pick_generation = 0;
    session->state = DF_GVS_TALKING;
    return DF_OK;
}
