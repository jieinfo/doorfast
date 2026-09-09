#ifndef DOORFAST_GVS_CALL_CONTROL_H
#define DOORFAST_GVS_CALL_CONTROL_H

#include "gvs_call_runtime.h"

#define DF_GVS_CALL_CONFIRM_TIMEOUT_MS 1000U

struct df_gvs_call_control {
    struct df_gvs_call_dispatch dispatch;
    struct df_gvs_call_ack acknowledgement;
    struct df_gvs_call_memory_sender sender;
};

struct df_gvs_call_control_result {
    struct df_gvs_call_runtime_result runtime;
    bool frame_ready;
    bool confirmation_started;
    bool dispatch_failed;
    bool dispatch_timed_out;
    bool dispatch_cancelled;
};

struct df_gvs_call_control_status {
    enum df_gvs_session_state session_state;
    uint64_t session_generation;
    enum df_gvs_call_command_type command_type;
    enum df_gvs_call_dispatch_state dispatch_state;
    enum df_gvs_call_ack_state acknowledgement_state;
    unsigned attempts;
};

int df_gvs_call_control_init(struct df_gvs_call_control *, uint64_t,
    df_gvs_header_provider_fn, void *);
int df_gvs_call_control_submit_answer(struct df_gvs_call_control *,
    const struct df_gvs_session *, uint64_t, const uint8_t local[6],
    uint16_t, uint16_t, uint8_t, uint64_t);
int df_gvs_call_control_submit_hangup(struct df_gvs_call_control *,
    const struct df_gvs_session *, uint64_t, const uint8_t local[6],
    uint8_t, uint64_t);
int df_gvs_call_control_step(struct df_gvs_call_control *,
    struct df_gvs_session *, const uint8_t local[6],
    struct df_gvs_deadline *, uint64_t, struct df_gvs_call_control_result *);
int df_gvs_call_control_receive(struct df_gvs_call_control *,
    const uint8_t *, size_t, const uint8_t local[6],
    struct df_gvs_session *, struct df_gvs_deadline *, uint64_t,
    struct df_gvs_call_control_result *);
int df_gvs_call_control_status(const struct df_gvs_call_control *,
    const struct df_gvs_session *, struct df_gvs_call_control_status *);
const char *df_gvs_session_state_name(enum df_gvs_session_state);
const char *df_gvs_call_command_type_name(enum df_gvs_call_command_type);
const char *df_gvs_call_dispatch_state_name(enum df_gvs_call_dispatch_state);
const char *df_gvs_call_ack_state_name(enum df_gvs_call_ack_state);

#endif
