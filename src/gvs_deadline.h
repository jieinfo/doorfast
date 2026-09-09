#ifndef DOORFAST_GVS_DEADLINE_H
#define DOORFAST_GVS_DEADLINE_H

#include <stdbool.h>
#include <stdint.h>

#include "gvs_session.h"

struct df_gvs_deadline {
    bool armed;
    uint64_t generation;
    uint64_t started_ms;
    uint64_t timeout_ms;
};

int df_gvs_deadline_arm(struct df_gvs_deadline *deadline,
                        const struct df_gvs_session *session,
                        uint64_t now_ms, uint64_t timeout_ms);
int df_gvs_deadline_tick(struct df_gvs_deadline *deadline,
                         struct df_gvs_session *session,
                         uint64_t now_ms, bool *timed_out);
void df_gvs_deadline_cancel(struct df_gvs_deadline *deadline);

#endif
