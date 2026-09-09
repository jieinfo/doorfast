#ifndef DOORFAST_GVS_CALL_DISPATCH_H
#define DOORFAST_GVS_CALL_DISPATCH_H
#include "gvs_call_command.h"
#include "gvs_send_transaction.h"

enum df_gvs_call_dispatch_state {
    DF_GVS_CALL_EMPTY, DF_GVS_CALL_QUEUED, DF_GVS_CALL_SENDING,
    DF_GVS_CALL_RETRY, DF_GVS_CALL_SENT, DF_GVS_CALL_FAILED,
    DF_GVS_CALL_TIMEOUT, DF_GVS_CALL_CANCELLED
};

/* Single-slot offline dispatch. SENT means local delivery, not peer acceptance.
 * All calls and callbacks must be serialized and non-reentrant. */
struct df_gvs_call_dispatch {
    enum df_gvs_call_dispatch_state state;
    struct df_gvs_call_command command;
    uint64_t last_now_ms, deadline_ms, next_id, active_id;
    unsigned attempts;
};
typedef enum df_gvs_send_attempt_result (*df_gvs_call_attempt_fn)(
    const struct df_gvs_call_command *, unsigned, uint64_t, void *);
struct df_gvs_call_memory_sender {
    df_gvs_header_provider_fn provide_fields;
    void *fields_context;
    uint8_t bytes[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
    size_t length;
    uint64_t completion_id;
};
enum df_gvs_send_attempt_result df_gvs_call_memory_attempt(
    const struct df_gvs_call_command *, unsigned, uint64_t, void *);
void df_gvs_call_dispatch_init(struct df_gvs_call_dispatch *, uint64_t);
int df_gvs_call_dispatch_enqueue(struct df_gvs_call_dispatch *,
    const struct df_gvs_call_command *, uint64_t);
int df_gvs_call_dispatch_step(struct df_gvs_call_dispatch *,
    const struct df_gvs_session *, const uint8_t local[6], uint64_t,
    df_gvs_call_attempt_fn, void *);
int df_gvs_call_dispatch_complete(struct df_gvs_call_dispatch *,
    const struct df_gvs_session *, const uint8_t local[6], uint64_t,
    uint64_t completion_id, enum df_gvs_send_attempt_result);
#endif
