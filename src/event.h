#ifndef DOORFAST_EVENT_H
#define DOORFAST_EVENT_H

#include <stddef.h>

enum df_event_type {
    DF_EVENT_UNKNOWN = 0,
    DF_EVENT_INCOMING_CALL,
    DF_EVENT_HANGUP,
};

struct df_event {
    enum df_event_type type;
    char call_id[128];
};

const char *df_event_type_name(enum df_event_type type);

#endif
