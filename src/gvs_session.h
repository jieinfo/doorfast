#ifndef DOORFAST_GVS_SESSION_H
#define DOORFAST_GVS_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "event.h"
#include "gvs_frame.h"

enum df_gvs_session_state {
    DF_GVS_IDLE = 0,
    DF_GVS_PREVIEW,
    DF_GVS_RINGING,
    DF_GVS_TALKING,
    DF_GVS_ENDED,
};

struct df_gvs_session {
    enum df_gvs_session_state state;
    uint8_t peer[6];
    uint64_t generation;
    uint64_t pick_generation;
    uint64_t pick_started_ms;
    uint64_t pick_timeout_ms;
    uint8_t pick_local[6];
};

struct df_gvs_transition_event {
    enum df_event_type type;
    uint8_t peer[6];
    uint64_t generation;
};

struct df_gvs_preemption {
    struct df_gvs_transition_event events[2];
    unsigned count;
    uint64_t ring_started_ms;
};

/* Caller must validate frame/target, classify both callers, and serialize access.
 * Only distinct callers while RINGING may preempt. No network/media side effects.
 * Caller must consume both ordered events and reset its timer on success.
 * On rejection session is unchanged and result is empty. */
int df_gvs_session_preempt(struct df_gvs_session *session,
    uint64_t expected_generation, const uint8_t incoming_peer[6],
    int current_category, int incoming_category, uint64_t now_ms,
    struct df_gvs_preemption *result);

/* Offline observation only. Timeout is caller policy, not a protocol constant. */
int df_gvs_session_observe_pick(struct df_gvs_session *session,
    const struct df_gvs_frame *frame, const uint8_t local[6],
    uint64_t now_ms, uint64_t timeout_ms);

int df_gvs_session_apply(struct df_gvs_session *session, const struct df_gvs_frame *frame,
                         const struct df_event *event);
int df_gvs_session_apply_for_generation(struct df_gvs_session *session, uint64_t generation,
                                        const struct df_gvs_frame *frame,
                                        const struct df_event *event);
int df_gvs_session_abort(struct df_gvs_session *session, bool *ended);

#endif
