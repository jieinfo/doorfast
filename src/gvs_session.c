#include "gvs_session.h"

#include <string.h>

static int df_gvs_session_is_active(const struct df_gvs_session *session) {
    return session->state == DF_GVS_PREVIEW || session->state == DF_GVS_RINGING ||
           session->state == DF_GVS_TALKING;
}

static void df_gvs_session_set_peer(struct df_gvs_session *session,
                                    const struct df_gvs_frame *frame) {
    memcpy(session->peer, frame->source, sizeof(session->peer));
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

    if (session->state == DF_GVS_IDLE && event->type == DF_EVENT_PREVIEW_STARTED) {
        df_gvs_session_set_peer(session, frame);
        session->state = DF_GVS_PREVIEW;
        return DF_OK;
    }
    if (session->state == DF_GVS_IDLE && event->type == DF_EVENT_INCOMING_CALL) {
        df_gvs_session_set_peer(session, frame);
        session->state = DF_GVS_RINGING;
        return DF_OK;
    }
    if ((session->state == DF_GVS_PREVIEW || session->state == DF_GVS_RINGING) &&
        event->type == DF_EVENT_SESSION_ESTABLISHED) {
        session->state = DF_GVS_TALKING;
        return DF_OK;
    }
    if (df_gvs_session_is_active(session) && event->type == DF_EVENT_HANGUP) {
        session->state = DF_GVS_ENDED;
        return DF_OK;
    }
    if (df_gvs_session_is_active(session) && event->type == DF_EVENT_UNLOCK_RESULT_OBSERVED) {
        return DF_OK;
    }
    return DF_ERR_INVALID;
}
