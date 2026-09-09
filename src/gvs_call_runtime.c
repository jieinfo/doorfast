#include "gvs_call_runtime.h"

#include <string.h>

static bool df_gvs_call_runtime_expected_ack(
    const struct df_gvs_call_ack *ack, const struct df_gvs_frame *frame) {
    if (frame->family != 0x03) {
        return false;
    }
    return (ack->command.type == DF_GVS_CALL_COMMAND_ANSWER &&
            frame->opcode == 0x83) ||
           (ack->command.type == DF_GVS_CALL_COMMAND_HANGUP &&
            frame->opcode == 0x82);
}

static void df_gvs_call_runtime_record_terminal(
    enum df_gvs_call_ack_state previous,
    const struct df_gvs_call_ack *ack,
    struct df_gvs_call_runtime_result *result) {
    if (previous == DF_GVS_CALL_ACK_WAITING &&
        ack->state == DF_GVS_CALL_ACK_EXPIRED) {
        result->acknowledgement_expired = true;
    }
    if (previous == DF_GVS_CALL_ACK_WAITING &&
        ack->state == DF_GVS_CALL_ACK_CANCELLED) {
        result->acknowledgement_cancelled = true;
    }
}

int df_gvs_call_runtime_tick(
    struct df_gvs_call_ack *ack, const uint8_t identity[6],
    struct df_gvs_session *session, struct df_gvs_deadline *deadline,
    uint64_t now_ms, struct df_gvs_call_runtime_result *result) {
    struct df_gvs_call_ack next_ack;
    struct df_gvs_session next_session;
    struct df_gvs_deadline next_deadline;
    struct df_gvs_call_runtime_result next_result = {0};

    if (ack == NULL || identity == NULL || session == NULL ||
        deadline == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    next_ack = *ack;
    next_session = *session;
    next_deadline = *deadline;
    /* Session expiry wins when both deadlines fall on the same tick. */
    if (df_gvs_deadline_tick(&next_deadline, &next_session, now_ms,
                             &next_result.session_timed_out) != DF_OK ||
        df_gvs_call_ack_tick(&next_ack, &next_session, identity, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    df_gvs_call_runtime_record_terminal(ack->state, &next_ack, &next_result);
    *ack = next_ack;
    *session = next_session;
    *deadline = next_deadline;
    *result = next_result;
    return DF_OK;
}

int df_gvs_call_runtime_receive(
    struct df_gvs_call_ack *ack, const uint8_t *data, size_t length,
    const uint8_t identity[6], struct df_gvs_session *session,
    struct df_gvs_deadline *deadline, uint64_t now_ms,
    struct df_gvs_call_runtime_result *result) {
    struct df_gvs_frame frame;
    struct df_event event;
    enum df_gvs_call_ack_state previous;
    int status;

    if (ack == NULL || data == NULL || identity == NULL || session == NULL ||
        deadline == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    memset(result, 0, sizeof(*result));
    if (df_gvs_frame_parse(data, length, &frame, &event) != DF_OK) {
        return DF_ERR_INVALID;
    }
    previous = ack->state;
    if (df_gvs_call_ack_tick(ack, session, identity, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    df_gvs_call_runtime_record_terminal(previous, ack, result);
    if (df_gvs_call_runtime_expected_ack(ack, &frame)) {
        if (ack->state != DF_GVS_CALL_ACK_WAITING ||
            df_gvs_call_ack_observe(ack, session, identity, &frame,
                                    now_ms) != DF_OK) {
            result->acknowledgement_rejected = true;
            return DF_OK;
        }
        result->acknowledgement_confirmed = true;
        return df_gvs_receive_datagram(data, length, identity, session,
                                       deadline, now_ms, &result->receive);
    }
    status = df_gvs_receive_datagram(data, length, identity, session, deadline,
                                     now_ms, &result->receive);
    if (status != DF_OK) {
        return status;
    }
    previous = ack->state;
    if (df_gvs_call_ack_tick(ack, session, identity, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    df_gvs_call_runtime_record_terminal(previous, ack, result);
    return DF_OK;
}
