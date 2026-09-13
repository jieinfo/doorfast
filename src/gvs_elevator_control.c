#include "gvs_elevator_control.h"

#include <limits.h>
#include <string.h>

static int df_gvs_elevator_local_valid(const uint8_t local[6]) {
    struct df_gvs_elevator_request request;

    return df_gvs_elevator_prepare_query(local, &request);
}

static void df_gvs_elevator_finish(
    struct df_gvs_elevator_control *control,
    enum df_gvs_elevator_control_state state) {
    control->state = state;
    control->next_send_ms = 0;
    control->deadline_ms = 0;
}

int df_gvs_elevator_control_init(
    struct df_gvs_elevator_control *control, uint64_t now_ms,
    df_gvs_elevator_send_fn send, void *send_context) {
    struct df_gvs_elevator_control next = {0};

    if (control == NULL || send == NULL)
        return DF_ERR_INVALID;
    next.state = DF_GVS_ELEVATOR_CONTROL_IDLE;
    next.last_now_ms = now_ms;
    next.send = send;
    next.send_context = send_context;
    *control = next;
    return DF_OK;
}

int df_gvs_elevator_control_submit(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    enum df_gvs_elevator_direction direction, uint64_t transaction_id,
    uint64_t now_ms) {
    struct df_gvs_elevator_control next;
    struct df_gvs_elevator_request request;
    int send_status;

    if (control == NULL || local == NULL || control->send == NULL ||
        control->state == DF_GVS_ELEVATOR_CONTROL_WAITING ||
        transaction_id == 0U || now_ms < control->last_now_ms ||
        now_ms > UINT64_MAX - DF_GVS_ELEVATOR_DEADLINE_MS ||
        df_gvs_elevator_prepare_call(local, direction, &request) != DF_OK)
        return DF_ERR_INVALID;
    next = *control;
    next.state = DF_GVS_ELEVATOR_CONTROL_WAITING;
    next.request = request;
    next.transaction_id = transaction_id;
    next.last_now_ms = now_ms;
    next.next_send_ms = now_ms + DF_GVS_ELEVATOR_RETRY_DELAY_MS;
    next.deadline_ms = now_ms + DF_GVS_ELEVATOR_DEADLINE_MS;
    next.attempts = 1U;
    next.successful_sends = 0U;
    next.physical_result_confirmed = false;
    send_status = next.send(&next.request, next.send_context);
    if (send_status == DF_OK)
        next.successful_sends = 1U;
    *control = next;
    return DF_OK;
}

int df_gvs_elevator_control_tick(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    uint64_t now_ms) {
    struct df_gvs_elevator_control next;
    int send_status;

    if (control == NULL || local == NULL || control->send == NULL ||
        now_ms < control->last_now_ms ||
        df_gvs_elevator_local_valid(local) != DF_OK)
        return DF_ERR_INVALID;
    next = *control;
    next.last_now_ms = now_ms;
    if (next.state != DF_GVS_ELEVATOR_CONTROL_WAITING) {
        *control = next;
        return DF_OK;
    }
    if (memcmp(next.request.source, local, 6) != 0) {
        df_gvs_elevator_finish(&next, DF_GVS_ELEVATOR_CONTROL_CANCELLED);
        *control = next;
        return DF_OK;
    }
    if (now_ms >= next.deadline_ms) {
        df_gvs_elevator_finish(&next, DF_GVS_ELEVATOR_CONTROL_EXPIRED);
        *control = next;
        return DF_OK;
    }
    if (next.attempts == 1U && now_ms >= next.next_send_ms) {
        next.attempts = 2U;
        send_status = next.send(&next.request, next.send_context);
        if (send_status == DF_OK)
            next.successful_sends++;
        else if (next.successful_sends == 0U)
            df_gvs_elevator_finish(
                &next, DF_GVS_ELEVATOR_CONTROL_SEND_FAILED);
    }
    *control = next;
    return DF_OK;
}

int df_gvs_elevator_control_observe(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    const struct df_gvs_frame *frame, uint64_t now_ms) {
    struct df_gvs_elevator_control next;

    if (control == NULL || local == NULL || frame == NULL ||
        control->state != DF_GVS_ELEVATOR_CONTROL_WAITING ||
        control->successful_sends == 0U || now_ms < control->last_now_ms ||
        now_ms >= control->deadline_ms ||
        df_gvs_elevator_local_valid(local) != DF_OK ||
        memcmp(control->request.source, local, 6) != 0 ||
        frame->family != 0x08 || frame->opcode != 0x82 ||
        memcmp(frame->source, control->request.destination, 6) != 0 ||
        memcmp(frame->destination, local, 6) != 0)
        return DF_ERR_INVALID;
    next = *control;
    next.last_now_ms = now_ms;
    next.physical_result_confirmed = false;
    df_gvs_elevator_finish(
        &next, DF_GVS_ELEVATOR_CONTROL_PROTOCOL_COMPLETED);
    *control = next;
    return DF_OK;
}

const char *df_gvs_elevator_control_state_name(
    enum df_gvs_elevator_control_state state) {
    switch (state) {
    case DF_GVS_ELEVATOR_CONTROL_IDLE: return "idle";
    case DF_GVS_ELEVATOR_CONTROL_WAITING: return "waiting";
    case DF_GVS_ELEVATOR_CONTROL_PROTOCOL_COMPLETED:
        return "protocol_completed";
    case DF_GVS_ELEVATOR_CONTROL_EXPIRED: return "expired";
    case DF_GVS_ELEVATOR_CONTROL_SEND_FAILED: return "send_failed";
    case DF_GVS_ELEVATOR_CONTROL_CANCELLED: return "cancelled";
    }
    return "unknown";
}
