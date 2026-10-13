#include "gvs_call_control.h"
#include "gvs_memory_sender.h"

#include <string.h>

static bool df_gvs_call_control_dispatch_active(
    enum df_gvs_call_dispatch_state state) {
    return state == DF_GVS_CALL_QUEUED || state == DF_GVS_CALL_SENDING ||
           state == DF_GVS_CALL_RETRY;
}

static bool df_gvs_call_control_session_active(enum df_gvs_session_state state) {
    return state == DF_GVS_PREVIEW || state == DF_GVS_RINGING ||
           state == DF_GVS_TALKING;
}

static void df_gvs_call_control_record_terminal(
    struct df_gvs_call_control *control,
    enum df_gvs_call_end_reason end_reason, enum df_gvs_call_fault fault,
    const struct df_gvs_session *session, const uint8_t station[6],
    uint64_t generation) {
    if (control == NULL || session == NULL || station == NULL ||
        generation == 0U) return;
    control->terminal.end_reason = end_reason;
    control->terminal.fault = fault;
    memcpy(control->terminal.station, station, sizeof(control->terminal.station));
    control->terminal.generation = generation;
    control->fault = fault;
}

static void df_gvs_call_control_classify_transition(
    struct df_gvs_call_control *control,
    const struct df_gvs_session *previous,
    const struct df_gvs_session *session,
    const struct df_gvs_call_control_result *result) {
    enum df_gvs_call_fault fault = DF_GVS_CALL_FAULT_NONE;

    if (control == NULL || previous == NULL || session == NULL || result == NULL)
        return;
    /* Rejected acknowledgements include stale, malformed, and unmatched
     * frames. The receive path has no distinct explicit peer-refusal signal. */
    if (result->runtime.acknowledgement_expired)
        fault = DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT;
    if (result->runtime.receive.preempted_session) {
        if (df_gvs_call_control_session_active(previous->state)) {
            df_gvs_call_control_record_terminal(control,
                DF_GVS_CALL_END_PREEMPTED, DF_GVS_CALL_FAULT_PREEMPTED,
                previous, previous->peer, previous->generation);
        }
        control->fault = DF_GVS_CALL_FAULT_NONE;
        control->observed_generation = session->generation;
        return;
    }
    if (result->runtime.session_timed_out &&
        df_gvs_call_control_session_active(previous->state)) {
        df_gvs_call_control_record_terminal(control,
            DF_GVS_CALL_END_SESSION_TIMEOUT, DF_GVS_CALL_FAULT_NONE,
            previous, previous->peer, previous->generation);
        control->observed_generation = session->generation;
        return;
    }
    if (result->runtime.receive.ended_transition &&
        df_gvs_call_control_session_active(previous->state) &&
        session->state == DF_GVS_ENDED) {
        df_gvs_call_control_record_terminal(control,
            result->runtime.receive.local_hangup
                ? DF_GVS_CALL_END_LOCAL_HANGUP
                : DF_GVS_CALL_END_PEER_HANGUP,
            DF_GVS_CALL_FAULT_NONE, previous, previous->peer,
            previous->generation);
        control->observed_generation = session->generation;
        return;
    }
    if (df_gvs_call_control_session_active(previous->state) &&
        session->state == DF_GVS_ENDED) {
        df_gvs_call_control_record_terminal(control,
            DF_GVS_CALL_END_UNKNOWN, fault, previous, previous->peer,
            previous->generation);
        control->observed_generation = session->generation;
        return;
    }
    if (fault != DF_GVS_CALL_FAULT_NONE) {
        control->fault = fault;
    } else if (!df_gvs_call_control_session_active(previous->state) &&
               df_gvs_call_control_session_active(session->state) &&
               previous->generation != session->generation) {
        control->fault = DF_GVS_CALL_FAULT_NONE;
    }
    control->observed_generation = session->generation;
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
    control->terminal.end_reason = DF_GVS_CALL_END_NONE;
    control->terminal.fault = DF_GVS_CALL_FAULT_NONE;
    control->fault = DF_GVS_CALL_FAULT_NONE;
    return DF_OK;
}

void df_gvs_call_control_set_sender(struct df_gvs_call_control *control,
    df_gvs_call_attempt_fn send, void *context) {
    if (control == NULL) return;
    control->call_attempt = send != NULL ? send : df_gvs_call_memory_attempt;
    control->call_attempt_context = send != NULL ? context : &control->sender;
}

void df_gvs_call_control_set_handshake_sender(
    struct df_gvs_call_control *control, df_gvs_call_attempt_fn send,
    void *context) {
    if (control == NULL) return;
    control->handshake_attempt = send;
    control->handshake_attempt_context = context;
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

int df_gvs_call_control_submit_call(
    struct df_gvs_call_control *control, const struct df_gvs_session *session,
    uint64_t expected_generation, const uint8_t local[6],
    uint16_t primary_media_port, uint16_t secondary_media_port,
    uint8_t duration_seconds, uint64_t now_ms) {
    struct df_gvs_call_command command;

    if (df_gvs_call_command_prepare_call(
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
    struct df_gvs_session previous_session;

    if (control == NULL || session == NULL || local == NULL ||
        deadline == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    next = *control;
    next_session = *session;
    next_deadline = *deadline;
    previous_session = next_session;
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
    df_gvs_call_control_classify_transition(&next, &previous_session,
        &next_session, &next_result);
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
            local, now_ms,
            next.handshake_attempt != NULL ? next.handshake_attempt :
                df_gvs_call_memory_attempt,
            next.handshake_attempt_context != NULL ?
                next.handshake_attempt_context : &next.handshake_sender) != DF_OK)
        return DF_ERR_INVALID;
    next_result.handshake_frame_ready = previous != DF_GVS_CALL_SENT &&
        next.handshake_dispatch.state == DF_GVS_CALL_SENT;
    if (handshake_enqueue(&next, &next_result.handshake.action, now_ms) != DF_OK)
        next_result.handshake_action_dropped = true;
    if (next_result.handshake.action.valid && !next_result.handshake_action_dropped) {
        if (df_gvs_call_dispatch_step(&next.handshake_dispatch, &next_session,
                local, now_ms,
                next.handshake_attempt != NULL ? next.handshake_attempt :
                    df_gvs_call_memory_attempt,
                next.handshake_attempt_context != NULL ?
                    next.handshake_attempt_context : &next.handshake_sender) != DF_OK)
            return DF_ERR_INVALID;
        next_result.handshake_frame_ready =
            next.handshake_dispatch.state == DF_GVS_CALL_SENT;
    }
    previous = next.dispatch.state;
    if (df_gvs_call_dispatch_step(
            &next.dispatch, &next_session, local, now_ms,
            next.call_attempt != NULL ? next.call_attempt : df_gvs_call_memory_attempt,
            next.call_attempt_context != NULL ? next.call_attempt_context : &next.sender) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_call_control_dispatch_active(previous)) {
        next_result.dispatch_failed =
            next.dispatch.state == DF_GVS_CALL_FAILED;
        next_result.dispatch_timed_out =
            next.dispatch.state == DF_GVS_CALL_TIMEOUT;
        next_result.dispatch_cancelled =
            next.dispatch.state == DF_GVS_CALL_CANCELLED;
        if (next_result.dispatch_failed)
            next.fault = DF_GVS_CALL_FAULT_DISPATCH_FAILED;
        else if (next_result.dispatch_timed_out)
            next.fault = DF_GVS_CALL_FAULT_DISPATCH_TIMEOUT;
        else if (next_result.dispatch_cancelled &&
                 !next_result.runtime.receive.preempted_session)
            next.fault = DF_GVS_CALL_FAULT_CANCELLED;
    }
    if (previous != DF_GVS_CALL_SENT &&
        next.dispatch.state == DF_GVS_CALL_SENT) {
        if (next.dispatch.command.type == DF_GVS_CALL_COMMAND_ANSWER ||
            next.dispatch.command.type == DF_GVS_CALL_COMMAND_CALL) {
            struct df_gvs_receive_result sent_result;
            uint8_t sent_frame[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
            size_t sent_length = 0;

            if (df_gvs_call_command_serialize(
                    &next.dispatch.command, sent_frame, sizeof(sent_frame),
                    &sent_length, df_gvs_placeholder_header_fields,
                    NULL) != DF_OK ||
                df_gvs_receive_datagram(
                    sent_frame, sent_length, local,
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
    struct df_gvs_session previous_session;

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
    previous_session = next_session;
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
    df_gvs_call_control_classify_transition(&next, &previous_session,
        &next_session, &next_result);
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
    case DF_GVS_CALL_COMMAND_CALL: return "call";
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

const char *df_gvs_call_end_reason_name(enum df_gvs_call_end_reason reason) {
    switch (reason) {
    case DF_GVS_CALL_END_NONE: return "none";
    case DF_GVS_CALL_END_LOCAL_HANGUP: return "local_hangup";
    case DF_GVS_CALL_END_PEER_HANGUP: return "peer_hangup";
    case DF_GVS_CALL_END_SESSION_TIMEOUT: return "session_timeout";
    case DF_GVS_CALL_END_CONFIRMATION_TIMEOUT: return "confirmation_timeout";
    case DF_GVS_CALL_END_PREEMPTED: return "preempted";
    case DF_GVS_CALL_END_RUNTIME_RESET: return "runtime_reset";
    case DF_GVS_CALL_END_UNKNOWN: return "unknown";
    default: return NULL;
    }
}

const char *df_gvs_call_fault_name(enum df_gvs_call_fault fault) {
    switch (fault) {
    case DF_GVS_CALL_FAULT_NONE: return "none";
    case DF_GVS_CALL_FAULT_DISPATCH_FAILED: return "dispatch_failed";
    case DF_GVS_CALL_FAULT_DISPATCH_TIMEOUT: return "dispatch_timeout";
    case DF_GVS_CALL_FAULT_CONFIRMATION_REJECTED: return "confirmation_rejected";
    case DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT: return "confirmation_timeout";
    case DF_GVS_CALL_FAULT_PREEMPTED: return "preempted";
    case DF_GVS_CALL_FAULT_CANCELLED: return "cancelled";
    case DF_GVS_CALL_FAULT_RUNTIME_RESET: return "runtime_reset";
    case DF_GVS_CALL_FAULT_UNKNOWN: return "unknown";
    default: return NULL;
    }
}

const char *df_gvs_call_timer_name(enum df_gvs_call_timer timer) {
    switch (timer) {
    case DF_GVS_CALL_TIMER_NONE: return "none";
    case DF_GVS_CALL_TIMER_CONFIRMATION: return "confirmation";
    case DF_GVS_CALL_TIMER_SESSION: return "session";
    default: return NULL;
    }
}

int df_gvs_call_control_status_with_deadline(
    const struct df_gvs_call_control *control,
    const struct df_gvs_session *session,
    const struct df_gvs_deadline *deadline,
    struct df_gvs_call_control_status *status) {
    struct df_gvs_call_control_status next;
    uint64_t now_ms;

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
    next.end_reason = df_gvs_call_control_session_active(session->state)
        ? DF_GVS_CALL_END_NONE : control->terminal.end_reason;
    next.fault = control->fault;
    memcpy(next.last_station, control->terminal.station,
        sizeof(next.last_station));
    next.last_generation = control->terminal.generation;
    next.timer = DF_GVS_CALL_TIMER_NONE;
    next.remaining_ms = 0U;
    now_ms = control->acknowledgement.last_now_ms;
    if (control->dispatch.last_now_ms > now_ms)
        now_ms = control->dispatch.last_now_ms;
    if (control->handshake.last_now_ms > now_ms)
        now_ms = control->handshake.last_now_ms;
    if (df_gvs_call_control_session_active(session->state) &&
        control->acknowledgement.state == DF_GVS_CALL_ACK_WAITING) {
        next.timer = DF_GVS_CALL_TIMER_CONFIRMATION;
        next.remaining_ms = now_ms < control->acknowledgement.deadline_ms
            ? control->acknowledgement.deadline_ms - now_ms : 0U;
    } else if (df_gvs_call_control_session_active(session->state) &&
               deadline != NULL && deadline->armed) {
        next.timer = DF_GVS_CALL_TIMER_SESSION;
        if (df_gvs_deadline_remaining(deadline, now_ms,
                &next.remaining_ms) != DF_OK)
            return DF_ERR_INVALID;
    }
    *status = next;
    return DF_OK;
}

int df_gvs_call_control_status(
    const struct df_gvs_call_control *control,
    const struct df_gvs_session *session,
    struct df_gvs_call_control_status *status) {
    return df_gvs_call_control_status_with_deadline(control, session, NULL,
        status);
}

int df_gvs_call_control_runtime_reset(struct df_gvs_call_control *control,
    struct df_gvs_session *session, struct df_gvs_deadline *deadline) {
    bool ended;

    if (control == NULL || session == NULL || deadline == NULL ||
        !df_gvs_call_control_session_active(session->state))
        return DF_ERR_INVALID;
    df_gvs_call_control_record_terminal(control,
        DF_GVS_CALL_END_RUNTIME_RESET, DF_GVS_CALL_FAULT_RUNTIME_RESET,
        session, session->peer, session->generation);
    if (df_gvs_session_abort(session, &ended) != DF_OK || !ended)
        return DF_ERR_INVALID;
    df_gvs_deadline_cancel(deadline);
    control->observed_generation = session->generation;
    return DF_OK;
}
