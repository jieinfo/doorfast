#include "session.h"

#include <string.h>

int df_session_apply(struct df_session *session, const struct df_event *event) {
    if (session == NULL || event == NULL || event->call_id[0] == '\0') {
        return DF_ERR_INVALID;
    }
    if (session->active && strcmp(session->call_id, event->call_id) != 0) {
        return DF_ERR_INVALID;
    }
    (void)strncpy(session->call_id, event->call_id, sizeof(session->call_id) - 1);
    session->call_id[sizeof(session->call_id) - 1] = '\0';
    session->last_event_type = event->type;
    session->active = event->type != DF_EVENT_HANGUP;
    return DF_OK;
}
