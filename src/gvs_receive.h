#ifndef DOORFAST_GVS_RECEIVE_H
#define DOORFAST_GVS_RECEIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_deadline.h"
#include "gvs_session.h"

struct df_gvs_receive_result {
    struct df_gvs_preemption transition;
    bool accepted_call;
    bool talking_transition;
    bool rejected_pick;
    bool ended_transition;
    bool observed_hangup;
    bool preempted_session;
    bool time_sync_update;
    bool rejected_time_sync;
    bool timed_out_transition;
};

int df_gvs_receive_datagram(const uint8_t *data, size_t length,
                            const uint8_t identity[6],
                            struct df_gvs_session *session,
                            struct df_gvs_deadline *deadline,
                            uint64_t now_ms,
                            struct df_gvs_receive_result *result);

#endif
