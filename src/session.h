#ifndef DOORFAST_SESSION_H
#define DOORFAST_SESSION_H

#include "doorfast.h"
#include "event.h"

struct df_session {
    char call_id[128];
    enum df_event_type last_event_type;
    int active;
};

int df_session_apply(struct df_session *session, const struct df_event *event);

#endif
