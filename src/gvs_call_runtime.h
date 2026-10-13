#ifndef DOORFAST_GVS_CALL_RUNTIME_H
#define DOORFAST_GVS_CALL_RUNTIME_H
#include "gvs_call_ack.h"
#include "gvs_receive.h"

enum df_gvs_call_end_reason {
    DF_GVS_CALL_END_NONE,
    DF_GVS_CALL_END_LOCAL_HANGUP,
    DF_GVS_CALL_END_PEER_HANGUP,
    DF_GVS_CALL_END_SESSION_TIMEOUT,
    DF_GVS_CALL_END_CONFIRMATION_TIMEOUT,
    DF_GVS_CALL_END_PREEMPTED,
    DF_GVS_CALL_END_RUNTIME_RESET,
    DF_GVS_CALL_END_HEARTBEAT_TIMEOUT,
    DF_GVS_CALL_END_UNKNOWN
};

enum df_gvs_call_fault {
    DF_GVS_CALL_FAULT_NONE,
    DF_GVS_CALL_FAULT_DISPATCH_FAILED,
    DF_GVS_CALL_FAULT_DISPATCH_TIMEOUT,
    DF_GVS_CALL_FAULT_CONFIRMATION_REJECTED,
    DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT,
    DF_GVS_CALL_FAULT_PREEMPTED,
    DF_GVS_CALL_FAULT_CANCELLED,
    DF_GVS_CALL_FAULT_RUNTIME_RESET,
    DF_GVS_CALL_FAULT_HEARTBEAT_TIMEOUT,
    DF_GVS_CALL_FAULT_UNKNOWN
};

enum df_gvs_call_timer {
    DF_GVS_CALL_TIMER_NONE,
    DF_GVS_CALL_TIMER_CONFIRMATION,
    DF_GVS_CALL_TIMER_SESSION
};

struct df_gvs_call_terminal {
    enum df_gvs_call_end_reason end_reason;
    enum df_gvs_call_fault fault;
    uint8_t station[6];
    uint64_t generation;
};

struct df_gvs_call_runtime_result {
    struct df_gvs_receive_result receive;
    bool acknowledgement_confirmed;
    bool acknowledgement_rejected;
    bool acknowledgement_expired;
    bool acknowledgement_cancelled;
    bool session_timed_out;
    bool local_hangup;
};

const char *df_gvs_call_end_reason_name(enum df_gvs_call_end_reason);
const char *df_gvs_call_fault_name(enum df_gvs_call_fault);
const char *df_gvs_call_timer_name(enum df_gvs_call_timer);
int df_gvs_call_runtime_tick(struct df_gvs_call_ack *,
    const uint8_t identity[6], struct df_gvs_session *,
    struct df_gvs_deadline *, uint64_t, struct df_gvs_call_runtime_result *);
int df_gvs_call_runtime_receive(struct df_gvs_call_ack *, const uint8_t *,
    size_t, const uint8_t identity[6], struct df_gvs_session *,
    struct df_gvs_deadline *, uint64_t, struct df_gvs_call_runtime_result *);
#endif
