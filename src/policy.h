#ifndef DOORFAST_POLICY_H
#define DOORFAST_POLICY_H

#include <stdbool.h>
#include <time.h>

#include "event.h"

enum df_decision {
    DF_DECISION_IGNORE = 0,
    DF_DECISION_NOTIFY,
    DF_DECISION_HANGUP,
    DF_DECISION_OPEN_AFTER_DELAY,
};

struct df_policy {
    bool dnd_active;
    bool schedule_active;
    int unlock_delay_seconds;
    int hangup_delay_seconds;
};

enum df_decision df_policy_decide(const struct df_policy *policy,
                                  const struct df_event *event, time_t now);

#endif
