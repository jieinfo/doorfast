#include "gvs_call_ack.h"

#include <limits.h>
#include <string.h>

static bool df_gvs_call_ack_command_valid(
    const struct df_gvs_call_command *command) {
    static const uint8_t zero[6] = {0};

    if (!command->valid || command->session_generation == 0U ||
        memcmp(command->source, zero, 6) == 0 ||
        memcmp(command->destination, zero, 6) == 0) {
        return false;
    }
    if (command->type == DF_GVS_CALL_COMMAND_ANSWER) {
        return command->opcode == 0x03 && command->payload_length == 7U &&
               command->payload[0] == 0x02 && command->payload[3] == 0U &&
               (command->payload[1] != 0U || command->payload[2] != 0U) &&
               (command->payload[4] != 0U || command->payload[5] != 0U) &&
               command->payload[6] != 0U;
    }
    return command->type == DF_GVS_CALL_COMMAND_HANGUP &&
           command->opcode == 0x02 && command->payload_length == 1U;
}

static bool df_gvs_call_ack_current(
    const struct df_gvs_call_ack *ack, const struct df_gvs_session *session,
    const uint8_t local[6]) {
    if (ack->command.session_generation != session->generation ||
        memcmp(ack->command.destination, session->peer, 6) != 0 ||
        memcmp(ack->command.source, local, 6) != 0) {
        return false;
    }
    if (ack->command.type == DF_GVS_CALL_COMMAND_ANSWER) {
        return session->state == DF_GVS_RINGING;
    }
    return ack->command.type == DF_GVS_CALL_COMMAND_HANGUP &&
           (session->state == DF_GVS_PREVIEW ||
            session->state == DF_GVS_RINGING ||
            session->state == DF_GVS_TALKING);
}

static bool df_gvs_call_ack_frame_matches(
    const struct df_gvs_call_ack *ack, const struct df_gvs_frame *frame) {
    if (frame->family != 0x03 ||
        memcmp(frame->source, ack->command.destination, 6) != 0 ||
        memcmp(frame->destination, ack->command.source, 6) != 0) {
        return false;
    }
    if (ack->command.type == DF_GVS_CALL_COMMAND_HANGUP) {
        return frame->opcode == 0x82 && frame->payload_length == 0U;
    }
    return ack->command.type == DF_GVS_CALL_COMMAND_ANSWER &&
           frame->opcode == 0x83 && frame->payload_length == 7U &&
           frame->payload != NULL && frame->payload[0] == 0U &&
           memcmp(frame->payload + 1, ack->command.payload + 1, 5) == 0 &&
           frame->payload[6] != 0U;
}

void df_gvs_call_ack_init(struct df_gvs_call_ack *ack, uint64_t now_ms) {
    if (ack == NULL) {
        return;
    }
    memset(ack, 0, sizeof(*ack));
    ack->last_now_ms = now_ms;
}

int df_gvs_call_ack_begin(
    struct df_gvs_call_ack *ack, const struct df_gvs_call_dispatch *dispatch,
    const struct df_gvs_session *session, const uint8_t local[6],
    uint64_t now_ms, uint64_t timeout_ms) {
    struct df_gvs_call_ack next;

    if (ack == NULL || dispatch == NULL || session == NULL || local == NULL ||
        ack->state == DF_GVS_CALL_ACK_WAITING ||
        dispatch->state != DF_GVS_CALL_SENT ||
        !df_gvs_call_ack_command_valid(&dispatch->command) ||
        timeout_ms == 0U || now_ms < ack->last_now_ms ||
        now_ms > UINT64_MAX - timeout_ms) {
        return DF_ERR_INVALID;
    }
    next = *ack;
    next.command = dispatch->command;
    next.state = DF_GVS_CALL_ACK_WAITING;
    next.last_now_ms = now_ms;
    next.deadline_ms = now_ms + timeout_ms;
    if (!df_gvs_call_ack_current(&next, session, local)) {
        return DF_ERR_INVALID;
    }
    *ack = next;
    return DF_OK;
}

int df_gvs_call_ack_observe(
    struct df_gvs_call_ack *ack, const struct df_gvs_session *session,
    const uint8_t local[6], const struct df_gvs_frame *frame,
    uint64_t now_ms) {
    if (ack == NULL || session == NULL || local == NULL || frame == NULL ||
        ack->state != DF_GVS_CALL_ACK_WAITING ||
        now_ms < ack->last_now_ms || now_ms >= ack->deadline_ms ||
        !df_gvs_call_ack_current(ack, session, local) ||
        !df_gvs_call_ack_frame_matches(ack, frame)) {
        return DF_ERR_INVALID;
    }
    ack->last_now_ms = now_ms;
    ack->deadline_ms = 0U;
    ack->state = DF_GVS_CALL_ACK_CONFIRMED;
    return DF_OK;
}

int df_gvs_call_ack_tick(
    struct df_gvs_call_ack *ack, const struct df_gvs_session *session,
    const uint8_t local[6], uint64_t now_ms) {
    if (ack == NULL || session == NULL || local == NULL ||
        now_ms < ack->last_now_ms) {
        return DF_ERR_INVALID;
    }
    ack->last_now_ms = now_ms;
    if (ack->state != DF_GVS_CALL_ACK_WAITING) {
        return DF_OK;
    }
    if (!df_gvs_call_ack_current(ack, session, local)) {
        ack->state = DF_GVS_CALL_ACK_CANCELLED;
        ack->deadline_ms = 0U;
    } else if (now_ms >= ack->deadline_ms) {
        ack->state = DF_GVS_CALL_ACK_EXPIRED;
        ack->deadline_ms = 0U;
    }
    return DF_OK;
}

int df_gvs_call_ack_receive(
    struct df_gvs_call_ack *ack, const struct df_gvs_session *session,
    const uint8_t local[6], const uint8_t *data, size_t length,
    uint64_t now_ms) {
    struct df_gvs_frame frame;
    struct df_event event;

    if (data == NULL ||
        df_gvs_frame_parse(data, length, &frame, &event) != DF_OK) {
        return DF_ERR_INVALID;
    }
    return df_gvs_call_ack_observe(ack, session, local, &frame, now_ms);
}
