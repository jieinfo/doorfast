#include "gvs_observer.h"

#include "gvs_frame.h"
#include "gvs_identity.h"
#include <string.h>
#include "gvs_priority.h"

int df_gvs_observe_datagram_batch(const uint8_t *data, size_t length,
    const uint8_t identity[6], struct df_gvs_session *session, uint64_t now_ms,
    struct df_gvs_observation *result) {
    struct df_gvs_frame frame;
    int status;
    if (result == NULL) return DF_ERR_INVALID;
    memset(result, 0, sizeof(*result));
    if (identity == NULL || session == NULL) return DF_ERR_INVALID;
    status = df_gvs_frame_parse(data, length, &frame, &result->parsed);
    if (status != DF_OK) return status;
    if (result->parsed.type != DF_EVENT_INCOMING_CALL ||
        !df_gvs_frame_is_for_identity(&frame, identity)) return DF_OK;

    if (session->state == DF_GVS_RINGING && memcmp(session->peer, frame.source, 6) != 0) {
        status = df_gvs_session_preempt(session, session->generation, frame.source,
            df_gvs_call_category(session->peer, identity),
            df_gvs_call_category(frame.source, identity), now_ms, &result->transition);
    } else {
        uint64_t previous = session->generation;
        /* Do not wrap generation when starting via the batch interface. */
        if ((session->state == DF_GVS_IDLE || session->state == DF_GVS_ENDED) &&
            previous == UINT64_MAX) return DF_ERR_INVALID;
        status = df_gvs_session_apply(session, &frame, &result->parsed);
        if (status == DF_OK && session->generation != previous) {
            result->transition.count = 1;
            result->transition.events[0].type = DF_EVENT_INCOMING_CALL;
            memcpy(result->transition.events[0].peer, session->peer, 6);
            result->transition.events[0].generation = session->generation;
            result->transition.ring_started_ms = now_ms;
        }
    }
    result->accepted = status == DF_OK;
    return status;
}

int df_gvs_observe_datagram(const uint8_t *data, size_t length,
                            const uint8_t identity[6],
                            struct df_gvs_session *session,
                            struct df_event *event, bool *accepted) {
    struct df_gvs_frame frame = {0};
    int result;

    if (identity == NULL || session == NULL || event == NULL || accepted == NULL) {
        return DF_ERR_INVALID;
    }
    *accepted = false;
    result = df_gvs_frame_parse(data, length, &frame, event);
    if (result != DF_OK) {
        return result;
    }
    if (event->type != DF_EVENT_INCOMING_CALL ||
        !df_gvs_frame_is_for_identity(&frame, identity)) {
        return DF_OK;
    }
    result = df_gvs_session_apply(session, &frame, event);
    if (result == DF_OK) {
        *accepted = true;
    }
    return result;
}
