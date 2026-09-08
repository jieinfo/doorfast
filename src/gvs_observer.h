#ifndef DOORFAST_GVS_OBSERVER_H
#define DOORFAST_GVS_OBSERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event.h"
#include "gvs_session.h"

struct df_gvs_observation {
    struct df_event parsed;
    bool accepted;
    struct df_gvs_preemption transition;
};

/* Offline-only batch API: zero events for ignored/repeated calls, one for a
 * new call, two (old HANGUP then new INCOMING_CALL) for a ringing preemption.
 * now_ms is caller-provided monotonic capture time, never wall-clock time. */
int df_gvs_observe_datagram_batch(const uint8_t *data, size_t length,
    const uint8_t identity[6], struct df_gvs_session *session, uint64_t now_ms,
    struct df_gvs_observation *result);

int df_gvs_observe_datagram(const uint8_t *data, size_t length,
                            const uint8_t identity[6],
                            struct df_gvs_session *session,
                            struct df_event *event, bool *accepted);

#endif
