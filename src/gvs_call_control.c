#include "gvs_call_control.h"

#include <string.h>

static bool df_gvs_call_control_dispatch_active(
    enum df_gvs_call_dispatch_state state) {
    return state == DF_GVS_CALL_QUEUED || state == DF_GVS_CALL_SENDING ||
           state == DF_GVS_CALL_RETRY;
}

int df_gvs_call_control_init(struct df_gvs_call_control *control,
    uint64_t now_ms, df_gvs_header_provider_fn provide_fields,
    void *fields_context) {
    if (control == NULL || provide_fields == NULL) {
        return DF_ERR_INVALID;
    }
    memset(control, 0, sizeof(*control));
    df_gvs_call_dispatch_init(&control->dispatch, now_ms);
    df_gvs_call_ack_init(&control->acknowledgement, now_ms);
    control->sender.provide_fields = provide_fields;
    control->sender.fields_context = fields_context;
    control->handshake_sender = control->sender;
    df_gvs_call_dispatch_init(&control->handshake_dispatch, now_ms);
    control->handshake.last_now_ms = now_ms;
    return DF_OK;
}

static int handshake_enqueue(struct df_gvs_call_control *control,
    const struct df_gvs_handshake_action *action, uint64_t now) {
    struct df_gvs_call_command command = {0};
    if (!action->valid) return DF_OK;
    command.valid = true;
    command.type = action->type == DF_GVS_HANDSHAKE_ASK
        ? DF_GVS_CALL_COMMAND_HAND_ASK : DF_GVS_CALL_COMMAND_HAND_REPLY;
    command.opcode = action->type == DF_GVS_HANDSHAKE_ASK ? 0x51 : 0x52;
    command.session_generation = action->session_generation;
    memcpy(command.destination, action->destination, 6);
    memcpy(command.source, action->source, 6);
    return df_gvs_call_dispatch_enqueue(&control->handshake_dispatch,
                                       &command, now);
}

static int df_gvs_call_control_enqueue(
    struct df_gvs_call_control *control,
    const struct df_gvs_call_command *command, uint64_t now_ms) {
    struct df_gvs_call_control next;

    if (control == NULL || command == NULL) {
        return DF_ERR_INVALID;
    }
    if (now_ms < control->acknowledgement.last_now_ms) {
        return DF_ERR_INVALID;
    }
    if (control->acknowledgement.state == DF_GVS_CALL_ACK_WAITING ||
        df_gvs_call_control_dispatch_active(control->dispatch.state)) {
        return DF_ERR_IO;
    }
    next = *control;
    if (df_gvs_call_dispatch_enqueue(&next.dispatch, command, now_ms) != DF_OK) {
        return DF_ERR_IO;
    }
    *control = next;
    return DF_OK;
}

int df_gvs_call_control_submit_answer(
    struct df_gvs_call_control *control, const struct df_gvs_session *session,
    uint64_t expected_generation, const uint8_t local[6],
    uint16_t primary_media_port, uint16_t secondary_media_port,
    uint8_t duration_seconds, uint64_t now_ms) {
    struct df_gvs_call_command command;

    if (df_gvs_call_command_prepare_answer(
            session, expected_generation, local, primary_media_port,
            secondary_media_port, duration_seconds, &command) != DF_OK) {
        return DF_ERR_INVALID;
    }
    return df_gvs_call_control_enqueue(control, &command, now_ms);
}

int df_gvs_call_control_submit_hangup(
    struct df_gvs_call_control *control, const struct df_gvs_session *session,
    uint64_t expected_generation, const uint8_t local[6], uint8_t reason,
    uint64_t now_ms) {
    struct df_gvs_call_command command;

    if (df_gvs_call_command_prepare_hangup(
            session, expected_generation, local, reason, &command) != DF_OK) {
        return DF_ERR_INVALID;
    }
    return df_gvs_call_control_enqueue(control, &command, now_ms);
}

int df_gvs_call_control_step(
    struct df_gvs_call_control *control, struct df_gvs_session *session,
    const uint8_t local[6], struct df_gvs_deadline *deadline,
    uint64_t now_ms, struct df_gvs_call_control_result *result) {
    struct df_gvs_call_control next;
    struct df_gvs_session next_session;
    struct df_gvs_deadline next_deadline;
    struct df_gvs_call_control_result next_result = {0};
    enum df_gvs_call_dispatch_state previous;

    if (control == NULL || session == NULL || local == NULL ||
        deadline == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    next = *control;
    next_session = *session;
    next_deadline = *deadline;
    if (df_gvs_handshake_tick(&next.handshake, &next_session, local, now_ms,
                              &next_result.handshake) != DF_OK)
        return DF_ERR_INVALID;
    if (next_result.handshake.disconnected)
        df_gvs_deadline_cancel(&next_deadline);
    if (df_gvs_call_runtime_tick(
            &next.acknowledgement, local, &next_session, &next_deadline,
            now_ms, &next_result.runtime) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (!next.handshake.active &&
        (next_session.state == DF_GVS_RINGING ||
         next_session.state == DF_GVS_TALKING ||
         next_session.state == DF_GVS_PREVIEW)) {
        if (df_gvs_handshake_start(&next.handshake, &next_session, local,
                                   now_ms) != DF_OK ||
            df_gvs_handshake_tick(&next.handshake, &next_session, local,
                now_ms, &next_result.handshake) != DF_OK)
            return DF_ERR_INVALID;
    }
    previous = next.handshake_dispatch.state;
    if (df_gvs_call_dispatch_step(&next.handshake_dispatch, &next_session,
            local, now_ms, df_gvs_call_memory_attempt,
            &next.handshake_sender) != DF_OK)
        return DF_ERR_INVALID;
    next_result.handshake_frame_ready = previous != DF_GVS_CALL_SENT &&
        next.handshake_dispatch.state == DF_GVS_CALL_SENT;
    if (handshake_enqueue(&next, &next_result.handshake.action, now_ms) != DF_OK)
        next_result.handshake_action_dropped = true;
    if (next_result.handshake.action.valid && !next_result.handshake_action_dropped) {
        if (df_gvs_call_dispatch_step(&next.handshake_dispatch, &next_session,
                local, now_ms, df_gvs_call_memory_attempt,
                &next.handshake_sender) != DF_OK)
            return DF_ERR_INVALID;
        next_result.handshake_frame_ready =
            next.handshake_dispatch.state == DF_GVS_CALL_SENT;
    }
    previous = next.dispatch.state;
    if (df_gvs_call_dispatch_step(
            &next.dispatch, &next_session, local, now_ms,
            df_gvs_call_memory_attempt, &next.sender) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_call_control_dispatch_active(previous)) {
        next_result.dispatch_failed =
            next.dispatch.state == DF_GVS_CALL_FAILED;
        next_result.dispatch_timed_out =
            next.dispatch.state == DF_GVS_CALL_TIMEOUT;
        next_result.dispatch_cancelled =
            next.dispatch.state == DF_GVS_CALL_CANCELLED;
    }
    if (previous != DF_GVS_CALL_SENT &&
        next.dispatch.state == DF_GVS_CALL_SENT) {
        if (next.dispatch.command.type == DF_GVS_CALL_COMMAND_ANSWER) {
            struct df_gvs_receive_result sent_result;

            if (df_gvs_receive_datagram(
                    next.sender.bytes, next.sender.length, local,
                    &next_session, &next_deadline, now_ms,
                    &sent_result) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
        if (df_gvs_call_ack_begin(
                &next.acknowledgement, &next.dispatch, &next_session, local,
                now_ms, DF_GVS_CALL_CONFIRM_TIMEOUT_MS) != DF_OK) {
            return DF_ERR_INVALID;
        }
        next_result.frame_ready = true;
        next_result.confirmation_started = true;
    }
    if (next_result.handshake_action_dropped && next.handshake_dropped < UINT64_MAX)
        next.handshake_dropped++;
    *control = next;
    *session = next_session;
    *deadline = next_deadline;
    *result = next_result;
    return DF_OK;
}

int df_gvs_call_control_receive(
    struct df_gvs_call_control *control, const uint8_t *data, size_t length,
    const uint8_t local[6], struct df_gvs_session *session,
    struct df_gvs_deadline *deadline, uint64_t now_ms,
    struct df_gvs_call_control_result *result) {
    struct df_gvs_call_control next;
    struct df_gvs_session next_session;
    struct df_gvs_deadline next_deadline;
    struct df_gvs_call_control_result next_result = {0};

    struct df_gvs_frame frame;
    struct df_event event;

    if (control == NULL || data == NULL || local == NULL || session == NULL ||
        deadline == NULL || result == NULL ||
        now_ms < control->dispatch.last_now_ms ||
        now_ms < control->handshake.last_now_ms ||
        now_ms < control->handshake_dispatch.last_now_ms) {
        return DF_ERR_INVALID;
    }
    next = *control;
    next_session = *session;
    next_deadline = *deadline;
    if (df_gvs_frame_parse(data, length, &frame, &event) != DF_OK)
        return DF_ERR_INVALID;
    if (frame.family == 3 && (frame.opcode == 0x51 || frame.opcode == 0x52)) {
        if (df_gvs_handshake_receive(&next.handshake, data, length,
                &next_session, local, now_ms, &next_result.handshake) != DF_OK)
            return DF_ERR_INVALID;
        if (handshake_enqueue(&next, &next_result.handshake.action, now_ms) != DF_OK)
            next_result.handshake_action_dropped = true;
        if (next_result.handshake_action_dropped && next.handshake_dropped < UINT64_MAX)
            next.handshake_dropped++;
        *control = next;
        *result = next_result;
        return DF_OK;
    }
    if (df_gvs_call_runtime_receive(
            &next.acknowledgement, data, length, local, &next_session,
            &next_deadline, now_ms, &next_result.runtime) != DF_OK) {
        return DF_ERR_INVALID;
    }
    *control = next;
    *session = next_session;
    *deadline = next_deadline;
    *result = next_result;
    return DF_OK;
}

const char *df_gvs_session_state_name(enum df_gvs_session_state state) {
    switch (state) {
    case DF_GVS_IDLE: return "idle";
    case DF_GVS_PREVIEW: return "preview";
    case DF_GVS_RINGING: return "ringing";
    case DF_GVS_TALKING: return "talking";
    case DF_GVS_ENDED: return "ended";
    default: return NULL;
    }
}

const char *df_gvs_call_command_type_name(enum df_gvs_call_command_type type) {
    switch (type) {
    case DF_GVS_CALL_COMMAND_NONE: return "none";
    case DF_GVS_CALL_COMMAND_ANSWER: return "answer";
    case DF_GVS_CALL_COMMAND_HANGUP: return "hangup";
    case DF_GVS_CALL_COMMAND_HAND_ASK: return "hand_ask";
    case DF_GVS_CALL_COMMAND_HAND_REPLY: return "hand_reply";
    default: return NULL;
    }
}

const char *df_gvs_call_dispatch_state_name(
    enum df_gvs_call_dispatch_state state) {
    switch (state) {
    case DF_GVS_CALL_EMPTY: return "empty";
    case DF_GVS_CALL_QUEUED: return "queued";
    case DF_GVS_CALL_SENDING: return "sending";
    case DF_GVS_CALL_RETRY: return "retry";
    case DF_GVS_CALL_SENT: return "sent";
    case DF_GVS_CALL_FAILED: return "failed";
    case DF_GVS_CALL_TIMEOUT: return "timeout";
    case DF_GVS_CALL_CANCELLED: return "cancelled";
    default: return NULL;
    }
}

const char *df_gvs_call_ack_state_name(enum df_gvs_call_ack_state state) {
    switch (state) {
    case DF_GVS_CALL_ACK_EMPTY: return "empty";
    case DF_GVS_CALL_ACK_WAITING: return "waiting";
    case DF_GVS_CALL_ACK_CONFIRMED: return "confirmed";
    case DF_GVS_CALL_ACK_EXPIRED: return "expired";
    case DF_GVS_CALL_ACK_CANCELLED: return "cancelled";
    default: return NULL;
    }
}

int df_gvs_call_control_status(
    const struct df_gvs_call_control *control,
    const struct df_gvs_session *session,
    struct df_gvs_call_control_status *status) {
    struct df_gvs_call_control_status next;

    if (control == NULL || session == NULL || status == NULL ||
        df_gvs_session_state_name(session->state) == NULL ||
        df_gvs_call_command_type_name(control->dispatch.command.type) == NULL ||
        df_gvs_call_dispatch_state_name(control->dispatch.state) == NULL ||
        df_gvs_call_dispatch_state_name(control->handshake_dispatch.state) == NULL ||
        df_gvs_call_ack_state_name(control->acknowledgement.state) == NULL) {
        return DF_ERR_INVALID;
    }
    memset(&next, 0, sizeof(next));
    next.session_state = session->state;
    next.session_generation = session->generation;
    next.command_type = control->dispatch.command.type;
    next.dispatch_state = control->dispatch.state;
    next.acknowledgement_state = control->acknowledgement.state;
    next.attempts = control->dispatch.attempts;
    next.handshake_active = control->handshake.active;
    next.handshake_missed = control->handshake.missed_replies;
    next.handshake_next_ms = control->handshake.active &&
        control->handshake.next_probe_ms > control->handshake.last_now_ms
        ? control->handshake.next_probe_ms - control->handshake.last_now_ms : 0;
    next.handshake_dropped = control->handshake_dropped;
    next.handshake_dispatch = control->handshake_dispatch.state;
    *status = next;
    return DF_OK;
}
