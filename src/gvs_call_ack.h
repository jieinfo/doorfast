#ifndef DOORFAST_GVS_CALL_ACK_H
#define DOORFAST_GVS_CALL_ACK_H
#include "gvs_call_dispatch.h"
#include "gvs_frame.h"

enum df_gvs_call_ack_state {
    DF_GVS_CALL_ACK_EMPTY, DF_GVS_CALL_ACK_WAITING,
    DF_GVS_CALL_ACK_CONFIRMED, DF_GVS_CALL_ACK_EXPIRED,
    DF_GVS_CALL_ACK_CANCELLED
};
struct df_gvs_call_ack {
    enum df_gvs_call_ack_state state;
    struct df_gvs_call_command command;
    uint64_t last_now_ms, deadline_ms;
};
void df_gvs_call_ack_init(struct df_gvs_call_ack *, uint64_t);
int df_gvs_call_ack_begin(struct df_gvs_call_ack *,
    const struct df_gvs_call_dispatch *, const struct df_gvs_session *,
    const uint8_t local[6], uint64_t now_ms, uint64_t timeout_ms);
int df_gvs_call_ack_observe(struct df_gvs_call_ack *,
    const struct df_gvs_session *, const uint8_t local[6],
    const struct df_gvs_frame *, uint64_t now_ms);
int df_gvs_call_ack_tick(struct df_gvs_call_ack *,
    const struct df_gvs_session *, const uint8_t local[6], uint64_t now_ms);
int df_gvs_call_ack_receive(struct df_gvs_call_ack *,
    const struct df_gvs_session *, const uint8_t local[6],
    const uint8_t *data, size_t length, uint64_t now_ms);
#endif
