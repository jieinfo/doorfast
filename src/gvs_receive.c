#include "gvs_receive.h"

#include <string.h>

#include "event.h"
#include "gvs_frame.h"
#include "gvs_identity.h"
#include "gvs_observer.h"

/* Observation policy values; these are not claimed vendor constants. */
#define DF_GVS_PICK_TIMEOUT_MS 3000
#define DF_GVS_RING_TIMEOUT_MS 30000
#define DF_GVS_TALK_TIMEOUT_MS 120000

static int df_gvs_receive_hangup(struct df_gvs_frame *frame,
                                 const uint8_t identity[6],
                                 struct df_gvs_session *session,
                                 struct df_gvs_deadline *deadline,
                                 struct df_gvs_receive_result *result) {
    struct df_event event = {.type = DF_EVENT_HANGUP};
    bool inbound = memcmp(frame->source, session->peer, 6) == 0 &&
        df_gvs_frame_is_for_identity(frame, identity);
    bool outbound = memcmp(frame->source, identity, 6) == 0 &&
        memcmp(frame->destination, session->peer, 6) == 0;

    if (!inbound && !outbound) {
        return DF_OK;
    }
    memcpy(frame->source, session->peer, 6);
    if (df_gvs_session_apply(session, frame, &event) == DF_OK) {
        df_gvs_deadline_cancel(deadline);
        result->ended_transition = true;
        result->observed_hangup = true;
    }
    return DF_OK;
}

static int df_gvs_receive_time_sync(const struct df_gvs_frame *frame,
                                    const uint8_t identity[6],
                                    struct df_gvs_session *session,
                                    struct df_gvs_deadline *deadline,
                                    uint64_t now_ms,
                                    struct df_gvs_receive_result *result) {
    unsigned limit = session->state == DF_GVS_TALKING ? 120U : 30U;

    if ((session->state != DF_GVS_TALKING && session->state != DF_GVS_RINGING) ||
        frame->payload_length != 1 ||
        memcmp(frame->source, session->peer, 6) != 0 ||
        memcmp(frame->destination, identity, 6) != 0 || frame->payload[0] > limit) {
        result->rejected_time_sync = true;
        return DF_OK;
    }
    if (df_gvs_deadline_arm(deadline, session, now_ms,
                            (uint64_t)frame->payload[0] * 1000U) != DF_OK) {
        return DF_ERR_INVALID;
    }
    result->time_sync_update = true;
    return df_gvs_deadline_tick(deadline, session, now_ms,
                                &result->timed_out_transition);
}

static int df_gvs_receive_pick(const struct df_gvs_frame *frame,
                               const uint8_t identity[6],
                               struct df_gvs_session *session,
                               struct df_gvs_deadline *deadline,
                               uint64_t now_ms,
                               struct df_gvs_receive_result *result) {
    enum df_gvs_session_state previous = session->state;

    if (df_gvs_session_observe_pick(session, frame, identity, now_ms,
                                    DF_GVS_PICK_TIMEOUT_MS) != DF_OK) {
        result->rejected_pick = true;
        return DF_OK;
    }
    if (previous != DF_GVS_TALKING && session->state == DF_GVS_TALKING) {
        result->talking_transition = true;
        return df_gvs_deadline_arm(deadline, session, now_ms,
                                   DF_GVS_TALK_TIMEOUT_MS);
    }
    return DF_OK;
}

static int df_gvs_receive_call(const uint8_t *data, size_t length,
                               const uint8_t identity[6],
                               struct df_gvs_session *session,
                               struct df_gvs_deadline *deadline,
                               uint64_t now_ms,
                               struct df_gvs_receive_result *result) {
    struct df_gvs_observation observation;
    unsigned i;
    int status = df_gvs_observe_datagram_batch(data, length, identity, session,
                                                now_ms, &observation);

    if (status != DF_OK) {
        return status;
    }
    result->accepted_call = observation.accepted;
    result->transition = observation.transition;
    for (i = 0; i < observation.transition.count; ++i) {
        if (observation.transition.events[i].type == DF_EVENT_HANGUP) {
            result->ended_transition = true;
            result->preempted_session = true;
            df_gvs_deadline_cancel(deadline);
        } else if (observation.transition.events[i].type == DF_EVENT_INCOMING_CALL) {
            if (df_gvs_deadline_arm(deadline, session,
                                    observation.transition.ring_started_ms,
                                    DF_GVS_RING_TIMEOUT_MS) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
    }
    return DF_OK;
}

int df_gvs_receive_datagram(const uint8_t *data, size_t length,
                            const uint8_t identity[6],
                            struct df_gvs_session *session,
                            struct df_gvs_deadline *deadline,
                            uint64_t now_ms,
                            struct df_gvs_receive_result *result) {
    struct df_gvs_frame frame = {0};
    struct df_event event = {0};
    int status;

    if (identity == NULL || session == NULL || deadline == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    memset(result, 0, sizeof(*result));
    status = df_gvs_frame_parse(data, length, &frame, &event);
    if (status != DF_OK) {
        return status;
    }
    if (frame.family == 3 && frame.opcode == 2 && frame.payload_length == 1) {
        return df_gvs_receive_hangup(&frame, identity, session, deadline, result);
    }
    if (frame.family == 3 && frame.opcode == 0x57) {
        return df_gvs_receive_time_sync(&frame, identity, session, deadline,
                                        now_ms, result);
    }
    if (frame.family == 3 && (frame.opcode == 3 || frame.opcode == 0x83)) {
        return df_gvs_receive_pick(&frame, identity, session, deadline, now_ms, result);
    }
    return df_gvs_receive_call(data, length, identity, session, deadline,
                               now_ms, result);
}
