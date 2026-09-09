#include "event.h"

const char *df_event_type_name(enum df_event_type type) {
    switch (type) {
    case DF_EVENT_PICK_REPLY_OBSERVED:
        return "PickReplyObserved";
    case DF_EVENT_INCOMING_CALL:
        return "IncomingCall";
    case DF_EVENT_HANGUP:
        return "Hangup";
    case DF_EVENT_STATION_OBSERVED:
        return "StationObserved";
    case DF_EVENT_PREVIEW_STARTED:
        return "PreviewStarted";
    case DF_EVENT_SESSION_ESTABLISHED:
        return "SessionEstablished";
    case DF_EVENT_UNLOCK_RESULT_OBSERVED:
        return "UnlockResultObserved";
    case DF_EVENT_UNKNOWN:
    default:
        return "Unknown";
    }
}
