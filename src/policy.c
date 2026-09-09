#include "policy.h"

enum df_decision df_policy_decide(const struct df_policy *policy,
                                  const struct df_event *event, time_t now) {
    (void)now;
    if (policy == NULL || event == NULL || event->type != DF_EVENT_INCOMING_CALL) {
        return DF_DECISION_IGNORE;
    }
    if (policy->dnd_active) {
        return policy->hangup_delay_seconds >= 0 ? DF_DECISION_HANGUP : DF_DECISION_NOTIFY;
    }
    if (policy->schedule_active && policy->unlock_delay_seconds >= 0) {
        return DF_DECISION_OPEN_AFTER_DELAY;
    }
    return DF_DECISION_NOTIFY;
}
