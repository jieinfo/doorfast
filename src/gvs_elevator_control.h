#ifndef DOORFAST_GVS_ELEVATOR_CONTROL_H
#define DOORFAST_GVS_ELEVATOR_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "gvs_elevator.h"

#define DF_GVS_ELEVATOR_RETRY_DELAY_MS 1000U
#define DF_GVS_ELEVATOR_DEADLINE_MS 2000U

enum df_gvs_elevator_control_state {
    DF_GVS_ELEVATOR_CONTROL_IDLE,
    DF_GVS_ELEVATOR_CONTROL_WAITING,
    DF_GVS_ELEVATOR_CONTROL_PROTOCOL_COMPLETED,
    DF_GVS_ELEVATOR_CONTROL_EXPIRED,
    DF_GVS_ELEVATOR_CONTROL_SEND_FAILED,
    DF_GVS_ELEVATOR_CONTROL_CANCELLED,
};

typedef int (*df_gvs_elevator_send_fn)(
    const struct df_gvs_elevator_request *, void *);

struct df_gvs_elevator_control {
    enum df_gvs_elevator_control_state state;
    struct df_gvs_elevator_request request;
    uint64_t transaction_id;
    uint64_t last_now_ms;
    uint64_t next_send_ms;
    uint64_t deadline_ms;
    unsigned attempts;
    unsigned successful_sends;
    bool physical_result_confirmed;
    df_gvs_elevator_send_fn send;
    void *send_context;
};

int df_gvs_elevator_control_init(
    struct df_gvs_elevator_control *control, uint64_t now_ms,
    df_gvs_elevator_send_fn send, void *send_context);
int df_gvs_elevator_control_submit(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    enum df_gvs_elevator_direction direction, uint64_t transaction_id,
    uint64_t now_ms);
int df_gvs_elevator_control_tick(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    uint64_t now_ms);
int df_gvs_elevator_control_observe(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    const struct df_gvs_frame *frame, uint64_t now_ms);
const char *df_gvs_elevator_control_state_name(
    enum df_gvs_elevator_control_state state);

#endif
