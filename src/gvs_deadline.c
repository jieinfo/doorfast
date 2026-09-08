#include "gvs_deadline.h"

#include <string.h>

static bool df_gvs_state_has_deadline(enum df_gvs_session_state state) {
    return state == DF_GVS_PREVIEW || state == DF_GVS_RINGING || state == DF_GVS_TALKING;
}

int df_gvs_deadline_arm(struct df_gvs_deadline *deadline,
                        const struct df_gvs_session *session,
                        uint64_t now_ms, uint64_t timeout_ms) {
    if (deadline == NULL || session == NULL ||
        !df_gvs_state_has_deadline(session->state)) {
        return DF_ERR_INVALID;
    }
    deadline->armed = true;
    deadline->generation = session->generation;
    deadline->started_ms = now_ms;
    deadline->timeout_ms = timeout_ms;
    return DF_OK;
}

int df_gvs_deadline_tick(struct df_gvs_deadline *deadline,
                         struct df_gvs_session *session,
                         uint64_t now_ms, bool *timed_out) {
    if (deadline == NULL || session == NULL || timed_out == NULL) {
        return DF_ERR_INVALID;
    }
    *timed_out = false;
    if (!deadline->armed) {
        return DF_OK;
    }
    if (deadline->generation != session->generation ||
        !df_gvs_state_has_deadline(session->state)) {
        df_gvs_deadline_cancel(deadline);
        return DF_OK;
    }
    if (now_ms < deadline->started_ms) {
        return DF_ERR_INVALID;
    }
    if (now_ms - deadline->started_ms < deadline->timeout_ms) {
        return DF_OK;
    }
    session->state = DF_GVS_ENDED;
    session->pick_generation = 0;
    df_gvs_deadline_cancel(deadline);
    *timed_out = true;
    return DF_OK;
}

void df_gvs_deadline_cancel(struct df_gvs_deadline *deadline) {
    if (deadline != NULL) {
        memset(deadline, 0, sizeof(*deadline));
    }
}
