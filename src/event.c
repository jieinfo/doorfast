#include "event.h"

const char *df_event_type_name(enum df_event_type type) {
    switch (type) {
    case DF_EVENT_INCOMING_CALL:
        return "IncomingCall";
    case DF_EVENT_HANGUP:
        return "Hangup";
    case DF_EVENT_UNKNOWN:
    default:
        return "Unknown";
    }
}
