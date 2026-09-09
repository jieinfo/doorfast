#include "audit.h"

#include <stdio.h>
#include <syslog.h>

static const char *df_decision_name(enum df_decision decision) {
    switch (decision) {
    case DF_DECISION_NOTIFY:
        return "notify";
    case DF_DECISION_HANGUP:
        return "hangup";
    case DF_DECISION_OPEN_AFTER_DELAY:
        return "open_after_delay";
    case DF_DECISION_IGNORE:
    default:
        return "ignore";
    }
}

void df_audit_format(char *dst, size_t dst_size, const struct df_event *event,
                     enum df_decision decision) {
    const char *event_name = "Unknown";
    const char *session = "-";

    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (event != NULL) {
        event_name = df_event_type_name(event->type);
        if (event->call_id[0] != '\0') {
            session = event->call_id;
        }
    }
    (void)snprintf(dst, dst_size, "event=%s decision=%s session=%s", event_name,
                   df_decision_name(decision), session);
}

void df_audit_event(const struct df_event *event, enum df_decision decision) {
    char record[256];

    df_audit_format(record, sizeof(record), event, decision);
    syslog(LOG_INFO, "%s", record);
}
