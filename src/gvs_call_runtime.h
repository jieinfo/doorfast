#ifndef DOORFAST_GVS_CALL_RUNTIME_H
#define DOORFAST_GVS_CALL_RUNTIME_H
#include "gvs_call_ack.h"
#include "gvs_receive.h"

struct df_gvs_call_runtime_result {
    struct df_gvs_receive_result receive;
    bool acknowledgement_confirmed;
    bool acknowledgement_rejected;
    bool acknowledgement_expired;
    bool acknowledgement_cancelled;
    bool session_timed_out;
};
int df_gvs_call_runtime_tick(struct df_gvs_call_ack *,
    const uint8_t identity[6], struct df_gvs_session *,
    struct df_gvs_deadline *, uint64_t, struct df_gvs_call_runtime_result *);
int df_gvs_call_runtime_receive(struct df_gvs_call_ack *, const uint8_t *,
    size_t, const uint8_t identity[6], struct df_gvs_session *,
    struct df_gvs_deadline *, uint64_t, struct df_gvs_call_runtime_result *);
#endif
