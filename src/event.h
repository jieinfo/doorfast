#ifndef DOORFAST_EVENT_H
#define DOORFAST_EVENT_H

#include <stddef.h>
#include <stdint.h>

enum df_event_type {
    DF_EVENT_UNKNOWN = 0,
    DF_EVENT_INCOMING_CALL,
    DF_EVENT_HANGUP,
};

struct df_event {
    enum df_event_type type;
    char call_id[128];
    char remote_host[64];
    uint16_t remote_port;
};

const char *df_event_type_name(enum df_event_type type);

#endif
