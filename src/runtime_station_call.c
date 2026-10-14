#include "runtime_station_call.h"

#include <stdio.h>
#include <string.h>

#define PREPARE_MS 30000U
#define END_MS 5000U

static struct df_runtime_call_binding *binding_for(struct df_runtime_ubus *u) {
    struct df_runtime_call_binding *b;
    if (u == NULL || u->submit_call != df_runtime_call_submit ||
        u->call_context == NULL) return NULL;
    b = u->call_context;
    return b->ubus == u && b->control != NULL && b->session != NULL &&
        b->deadline != NULL && b->identity != NULL ? b : NULL;
}

bool df_runtime_station_call_active(const struct df_runtime_ubus *u) {
    return u != NULL && u->station_call.stage >= DF_RUNTIME_STATION_CALL_PREPARING &&
        u->station_call.stage <= DF_RUNTIME_STATION_CALL_ENDING;
}

static bool main_active(const struct df_gvs_session *s) {
    return s->state == DF_GVS_PREVIEW || s->state == DF_GVS_RINGING ||
        s->state == DF_GVS_TALKING;
}

static bool slot_busy(const struct df_gvs_call_control *c) {
    return c->dispatch.state == DF_GVS_CALL_QUEUED ||
        c->dispatch.state == DF_GVS_CALL_SENDING ||
        c->dispatch.state == DF_GVS_CALL_RETRY ||
        c->acknowledgement.state == DF_GVS_CALL_ACK_WAITING;
}

/* Copies the module's public snapshot; no manager internals in the daemon. */
static int media_snapshot(struct df_runtime_ubus *u, const char *station,
    struct df_media_session_status_v3 *out) {
    struct df_runtime_media_module *m = u->media;
    struct df_media_module_status_v3 status;
    size_t i;
    memset(out, 0, sizeof(*out));
    if (m == NULL || !m->available || m->session_snapshot == NULL)
        return DF_ERR_IO;
    memset(&status, 0, sizeof(status));
    status.sessions = m->session_snapshot;
    status.session_count = m->session_snapshot_capacity;
    if (df_runtime_media_module_status(m, &status) != DF_OK ||
        status.required_session_count > m->session_snapshot_capacity ||
        status.session_count > m->session_snapshot_capacity) return DF_ERR_IO;
    for (i = 0; i < status.session_count; ++i) {
        if (strcmp(status.sessions[i].station_id, station) == 0) {
            *out = status.sessions[i];
            break;
        }
    }
    if (!out->active && strcmp(status.failed_station_id, station) == 0 &&
        status.failed_generation == u->station_call.media_generation)
        out->last_error = status.failed_error != DF_MEDIA_ERROR_NONE ?
            status.failed_error : DF_MEDIA_ERROR_VIDEO_STALLED;
    return DF_OK;
}

static bool owner_matches(struct df_runtime_call_binding *b) {
    if (b->ubus == NULL) return false;
    struct df_runtime_station_call_operation *op = &b->ubus->station_call;
    const struct df_station *s = df_station_registry_find(b->stations, op->station_id);
    return s != NULL && op->control_generation != 0 &&
        b->session->generation == op->control_generation &&
        memcmp(b->session->peer, s->logical_address, 6) == 0;
}

static void cancel_commands(struct df_runtime_call_binding *b, uint64_t generation) {
    struct df_gvs_call_control *c = b->control;
    if (generation == 0) return;
    if (c->dispatch.command.session_generation == generation) {
        c->dispatch.state = DF_GVS_CALL_CANCELLED;
        c->dispatch.active_id = c->dispatch.deadline_ms = 0;
    }
    if (c->acknowledgement.command.session_generation == generation) {
        c->acknowledgement.state = DF_GVS_CALL_ACK_CANCELLED;
        c->acknowledgement.deadline_ms = 0;
    }
    if (c->handshake_dispatch.command.session_generation == generation) {
        c->handshake_dispatch.state = DF_GVS_CALL_CANCELLED;
        c->handshake_dispatch.active_id = c->handshake_dispatch.deadline_ms = 0;
    }
    if (owner_matches(b)) c->handshake.active = false;
}

static enum df_media_outbound_operation release_reason(
    enum df_gvs_call_end_reason reason) {
    switch (reason) {
    case DF_GVS_CALL_END_PEER_HANGUP: return DF_MEDIA_OUTBOUND_RELEASE_PEER;
    case DF_GVS_CALL_END_SESSION_TIMEOUT: return DF_MEDIA_OUTBOUND_RELEASE_SESSION;
    case DF_GVS_CALL_END_CONFIRMATION_TIMEOUT: return DF_MEDIA_OUTBOUND_RELEASE_CONFIRMATION;
    case DF_GVS_CALL_END_PREEMPTED: return DF_MEDIA_OUTBOUND_RELEASE_PREEMPTED;
    case DF_GVS_CALL_END_RUNTIME_RESET: return DF_MEDIA_OUTBOUND_RELEASE_RUNTIME;
    case DF_GVS_CALL_END_LOCAL_HANGUP: return DF_MEDIA_OUTBOUND_RELEASE;
    default: return DF_MEDIA_OUTBOUND_RELEASE_UNKNOWN;
    }
}

static int finish(struct df_runtime_call_binding *b, int error,
    enum df_gvs_call_end_reason reason, enum df_gvs_call_fault fault, uint64_t now) {
    struct df_runtime_ubus *u = b->ubus;
    struct df_runtime_station_call_operation *op = &u->station_call;
    struct df_media_session_status_v3 media;
    const struct df_media_session_key key = {op->station_id, op->media_generation};
    int cleanup = DF_OK;
    if (error != DF_OK && op->command_sent && owner_matches(b) &&
        main_active(b->session) && op->stage != DF_RUNTIME_STATION_CALL_ENDING &&
        reason != DF_GVS_CALL_END_PREEMPTED && reason != DF_GVS_CALL_END_RUNTIME_RESET &&
        now <= UINT64_MAX - END_MS) {
        cancel_commands(b, op->control_generation);
        int sent = df_gvs_call_control_submit_hangup(b->control, b->session,
            op->control_generation, b->identity, 1, now);
        if (sent == DF_OK) {
            op->stage = DF_RUNTIME_STATION_CALL_ENDING;
            op->deadline_ms = now + END_MS;
            op->error = error;
            op->end_reason = reason;
            op->fault = fault;
            return error;
        }
    }
    if (op->stage == DF_RUNTIME_STATION_CALL_ENDING && op->error != DF_OK) {
        error = op->error;
        reason = op->end_reason;
        fault = op->fault;
    }
    cancel_commands(b, op->control_generation);
    if (owner_matches(b)) {
        bool ended;
        (void)df_gvs_session_abort(b->session, &ended);
        df_gvs_deadline_cancel(b->deadline);
        b->control->terminal = (struct df_gvs_call_terminal){
            .end_reason = reason, .fault = fault, .generation = op->control_generation};
        memcpy(b->control->terminal.station, b->session->peer, 6);
        b->control->fault = fault;
    }
    if (media_snapshot(u, op->station_id, &media) != DF_OK) cleanup = DF_ERR_IO;
    else if (media.active && media.generation == op->media_generation) {
        if (media.viewer_active) op->owns_preview = false;
        if (op->promoted) {
            cleanup = df_runtime_media_module_outbound_operation(u->media,
                op->command_sent ? release_reason(reason) : DF_MEDIA_OUTBOUND_ROLLBACK,
                &key, op->control_generation, now);
            if (cleanup == DF_OK && !op->command_sent) op->promoted = false;
        }
        if (cleanup == DF_OK && !op->command_sent && op->owns_preview)
            cleanup = df_runtime_media_module_command(u->media,
                DF_MEDIA_MODULE_COMMAND_STOP, &key, false, now);
    }
    op->cleanup_error = cleanup;
    op->error = error != DF_OK ? error : cleanup;
    op->end_reason = reason;
    op->fault = fault;
    op->deadline_ms = 0;
    op->stage = op->error == DF_OK ? DF_RUNTIME_STATION_CALL_ENDED :
        DF_RUNTIME_STATION_CALL_FAILED;
    return op->error;
}

int df_runtime_station_call_admit(struct df_runtime_ubus *u, const char *station,
    uint64_t *generation) {
    struct df_runtime_call_binding *b = binding_for(u);
    struct df_media_session_status_v3 media;
    const struct df_station *s;
    struct df_runtime_station_call_operation next = {0};
    uint64_t now;
    int result;
    if (b == NULL || station == NULL || generation == NULL || u->media == NULL ||
        u->media->lifecycle_api == NULL) return DF_ERR_IO;
    now = u->last_now_ms;
    s = df_station_registry_find(b->stations, station);
    if (s == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!s->enabled) return DF_MEDIA_ERROR_STATION_DISABLED;
    if (df_runtime_station_call_active(u)) {
        if (strcmp(u->station_call.station_id, station) != 0 ||
            u->station_call.stage == DF_RUNTIME_STATION_CALL_ENDING)
            return DF_MEDIA_ERROR_CAPACITY_BUSY;
        *generation = u->station_call.media_generation;
        return DF_OK;
    }
    if (main_active(b->session)) {
        if ((b->session->state == DF_GVS_RINGING || b->session->state == DF_GVS_TALKING) &&
            memcmp(b->session->peer, s->logical_address, 6) == 0 &&
            media_snapshot(u, station, &media) == DF_OK && media.active &&
            media.purpose == DF_MEDIA_SESSION_CALL) {
            *generation = media.generation;
            return DF_OK;
        }
        return DF_MEDIA_ERROR_CAPACITY_BUSY;
    }
    if (slot_busy(b->control)) return DF_MEDIA_ERROR_CAPACITY_BUSY;
    if (u->next_station_call_operation_id == UINT64_MAX ||
        df_runtime_control_generation_next(b->session->generation) == 0 ||
        b->control->dispatch.next_id == 0 || now > UINT64_MAX - 120000U ||
        now < b->control->dispatch.last_now_ms ||
        now < b->control->acknowledgement.last_now_ms) return DF_ERR_IO;
    result = media_snapshot(u, station, &media);
    if (result != DF_OK) return result;
    if (media.active && (media.purpose != DF_MEDIA_SESSION_PREVIEW ||
        media.state == DF_MEDIA_SESSION_STOPPING)) return DF_MEDIA_ERROR_CAPACITY_BUSY;
    next.owns_preview = !media.active;
    result = df_runtime_media_module_request_start(u->media, station, now, generation);
    if (result != DF_OK) return result;
    next.operation_id = ++u->next_station_call_operation_id;
    (void)snprintf(next.station_id, sizeof(next.station_id), "%s", station);
    next.media_generation = *generation;
    next.started_ms = now;
    next.deadline_ms = now + PREPARE_MS;
    next.stage = DF_RUNTIME_STATION_CALL_PREPARING;
    u->station_call = next;
    if (strcmp(u->manual_unlock_station_id, station) == 0 &&
        u->manual_unlock_media_generation == *generation)
        u->manual_unlock_owns_preview = false;
    return DF_OK;
}

/* Only the coordinator's current, module-confirmed preparation may install. */
int df_runtime_station_call_install(struct df_runtime_call_binding *b,
    const struct df_runtime_call_request *r, uint64_t now) {
    struct df_runtime_ubus *u;
    const struct df_station *s;
    struct df_runtime_station_call_operation *op;
    struct df_gvs_session previous;
    struct df_media_session_key key;
    int result;
    if (b == NULL || r == NULL || r->type != DF_GVS_CALL_COMMAND_CALL) return DF_ERR_INVALID;
    u = b->ubus;
    if (u == NULL || binding_for(u) != b || !df_runtime_id_is_valid(r->runtime_id) ||
        strcmp(r->runtime_id, u->runtime_id) != 0 || u->media == NULL ||
        u->media->lifecycle_api == NULL) return DF_ERR_INVALID;
    op = &u->station_call;
    s = df_station_registry_find(b->stations, r->station_id);
    if (s == NULL || !s->enabled || main_active(b->session) || slot_busy(b->control) ||
        op->stage != DF_RUNTIME_STATION_CALL_PREPARING || op->promoted ||
        strcmp(op->station_id, r->station_id) != 0 ||
        r->media_generation != op->media_generation || r->media_generation == 0 ||
        r->session_generation == 0 ||
        r->session_generation != df_runtime_control_generation_next(b->session->generation) ||
        now < op->started_ms || now >= op->deadline_ms || now > UINT64_MAX - 120000U ||
        r->primary_media_port != 8303 || r->secondary_media_port != 8302 ||
        r->duration_seconds != 120 || r->reason != 0) return DF_ERR_INVALID;
    key = (struct df_media_session_key){op->station_id, op->media_generation};
    result = df_runtime_media_module_call_key(u->media, &key, now);
    if (result != DF_OK) return result;
    op->promoted = true;
    result = df_runtime_media_module_outbound_operation(u->media,
        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, r->session_generation, now);
    if (result != DF_OK)
        return finish(b, result, DF_GVS_CALL_END_UNKNOWN, DF_GVS_CALL_FAULT_DISPATCH_FAILED, now);
    op->control_generation = r->session_generation;
    previous = *b->session;
    memset(b->session, 0, sizeof(*b->session));
    b->session->state = DF_GVS_PREVIEW;
    b->session->generation = r->session_generation;
    memcpy(b->session->peer, s->logical_address, 6);
    result = df_gvs_call_control_submit_call(b->control, b->session,
        r->session_generation, b->identity, r->primary_media_port,
        r->secondary_media_port, r->duration_seconds, now);
    if (result != DF_OK) {
        *b->session = previous;
        return finish(b, result, DF_GVS_CALL_END_UNKNOWN, DF_GVS_CALL_FAULT_DISPATCH_FAILED, now);
    }
    else {
        df_gvs_deadline_cancel(b->deadline);
        b->control->handshake.active = false;
        b->control->fault = DF_GVS_CALL_FAULT_NONE;
        b->control->observed_generation = r->session_generation;
        op->stage = DF_RUNTIME_STATION_CALL_CALLING;
    }
    return result;
}

int df_runtime_station_call_step(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_call_binding *b = binding_for(u);
    struct df_runtime_station_call_operation *op;
    struct df_media_session_status_v3 media;
    struct df_media_session_key key;
    int result;
    if (!df_runtime_station_call_active(u)) return DF_OK;
    if (b == NULL) return DF_ERR_INVALID;
    op = &u->station_call;
    key = (struct df_media_session_key){op->station_id, op->media_generation};
    if (now < op->started_ms) return DF_ERR_INVALID;
    result = media_snapshot(u, op->station_id, &media);
    if (op->stage != DF_RUNTIME_STATION_CALL_ENDING &&
        (result != DF_OK || !media.active || media.generation != op->media_generation)) {
        bool failed = result != DF_OK || !media.active || media.last_error != DF_MEDIA_ERROR_NONE;
        int error = result != DF_OK ? result : media.last_error != DF_MEDIA_ERROR_NONE ?
            (int)media.last_error : failed ? DF_MEDIA_ERROR_VIDEO_STALLED :
            DF_MEDIA_ERROR_SESSION_PREEMPTED;
        return finish(b, error,
            failed ? DF_GVS_CALL_END_UNKNOWN : DF_GVS_CALL_END_PREEMPTED,
            failed ? DF_GVS_CALL_FAULT_UNKNOWN : DF_GVS_CALL_FAULT_PREEMPTED, now);
    }
    if (media.viewer_active) op->owns_preview = false;
    if (op->stage == DF_RUNTIME_STATION_CALL_PREPARING) {
        struct df_runtime_call_request request = {.type = DF_GVS_CALL_COMMAND_CALL,
            .session_generation = df_runtime_control_generation_next(b->session->generation),
            .media_generation = op->media_generation, .primary_media_port = 8303,
            .secondary_media_port = 8302, .duration_seconds = 120};
        if (main_active(b->session))
            return finish(b, DF_MEDIA_ERROR_SESSION_PREEMPTED, DF_GVS_CALL_END_PREEMPTED,
                DF_GVS_CALL_FAULT_PREEMPTED, now);
        if (now >= op->deadline_ms)
            return finish(b, DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED,
                DF_GVS_CALL_END_CONFIRMATION_TIMEOUT, DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT, now);
        result = df_runtime_media_module_outbound_operation(u->media,
            DF_MEDIA_OUTBOUND_CHECK, &key, 0, now);
        if (result == DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED) return DF_OK;
        if (result == DF_OK) {
            (void)snprintf(request.runtime_id, sizeof(request.runtime_id), "%s", u->runtime_id);
            (void)snprintf(request.station_id, sizeof(request.station_id), "%s", op->station_id);
            result = df_runtime_call_submit(&request, now, b);
        }
        if (result != DF_OK && df_runtime_station_call_active(u))
            return finish(b, result, DF_GVS_CALL_END_UNKNOWN, DF_GVS_CALL_FAULT_DISPATCH_FAILED, now);
        return DF_OK;
    }
    if (!owner_matches(b))
        return finish(b, DF_MEDIA_ERROR_SESSION_PREEMPTED, DF_GVS_CALL_END_PREEMPTED,
            DF_GVS_CALL_FAULT_PREEMPTED, now);
    if (!op->command_sent && b->control->dispatch.state == DF_GVS_CALL_SENT &&
        b->control->dispatch.command.type == DF_GVS_CALL_COMMAND_CALL) {
        /* Dispatch SENT is actual successful transport delivery, never enqueue. */
        op->command_sent = true;
        result = df_runtime_media_module_outbound_operation(u->media,
            DF_MEDIA_OUTBOUND_MARK_SENT, &key, op->control_generation, now);
        if (result != DF_OK)
            return finish(b, result, DF_GVS_CALL_END_UNKNOWN, DF_GVS_CALL_FAULT_UNKNOWN, now);
    }
    if (!main_active(b->session)) {
        struct df_gvs_call_terminal *t = &b->control->terminal;
        int error = op->error;
        if (error == DF_OK && t->generation == op->control_generation &&
            t->fault != DF_GVS_CALL_FAULT_NONE) error = DF_ERR_IO;
        return finish(b, error, t->generation == op->control_generation ?
            t->end_reason : DF_GVS_CALL_END_UNKNOWN,
            t->generation == op->control_generation ? t->fault : DF_GVS_CALL_FAULT_UNKNOWN, now);
    }
    if (op->stage != DF_RUNTIME_STATION_CALL_ENDING &&
        (media.purpose != DF_MEDIA_SESSION_CALL || media.state == DF_MEDIA_SESSION_FAILED ||
         media.state == DF_MEDIA_SESSION_STOPPING))
        return finish(b, DF_MEDIA_ERROR_VIDEO_STALLED, DF_GVS_CALL_END_UNKNOWN,
            DF_GVS_CALL_FAULT_UNKNOWN, now);
    if (b->control->dispatch.state == DF_GVS_CALL_FAILED ||
        b->control->dispatch.state == DF_GVS_CALL_TIMEOUT ||
        b->control->dispatch.state == DF_GVS_CALL_CANCELLED)
        return finish(b, DF_ERR_IO, DF_GVS_CALL_END_UNKNOWN, b->control->fault, now);
    if (b->control->acknowledgement.state == DF_GVS_CALL_ACK_EXPIRED)
        return finish(b, DF_ERR_IO, DF_GVS_CALL_END_CONFIRMATION_TIMEOUT,
            DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT, now);
    if (op->stage == DF_RUNTIME_STATION_CALL_ENDING && now >= op->deadline_ms)
        return finish(b, DF_ERR_IO, DF_GVS_CALL_END_LOCAL_HANGUP,
            DF_GVS_CALL_FAULT_DISPATCH_TIMEOUT, now);
    if (b->session->state == DF_GVS_TALKING && op->stage != DF_RUNTIME_STATION_CALL_ENDING)
        op->stage = DF_RUNTIME_STATION_CALL_TALKING;
    return DF_OK;
}

int df_runtime_call_step(struct df_runtime_call_binding *b, uint64_t now,
    struct df_gvs_call_control_result *result) {
    int status;
    if (b == NULL) return DF_ERR_INVALID;
    (void)df_runtime_station_call_step(b->ubus, now);
    status = df_gvs_call_control_step(b->control, b->session, b->identity,
        b->deadline, now, result);
    (void)df_runtime_station_call_step(b->ubus, now);
    df_runtime_call_release_ended(b, now);
    return status;
}

void df_runtime_call_release_ended(struct df_runtime_call_binding *b, uint64_t now) {
    struct df_media_session_status_v3 media;
    enum df_media_outbound_operation operation = DF_MEDIA_INCOMING_RELEASE_UNKNOWN;
    const struct df_station *station = NULL;
    if (b == NULL || b->ubus == NULL || b->session->state != DF_GVS_ENDED ||
        b->ubus->media == NULL || !b->ubus->media->available || b->stations == NULL)
        return;
    for (size_t i = 0; i < b->stations->count; ++i)
        if (memcmp(b->stations->items[i].logical_address, b->session->peer, 6) == 0)
            station = &b->stations->items[i];
    if (station == NULL || media_snapshot(b->ubus, station->id, &media) != DF_OK ||
        !media.active || media.purpose != DF_MEDIA_SESSION_CALL) return;
    switch (b->control->terminal.end_reason) {
    case DF_GVS_CALL_END_LOCAL_HANGUP: operation = DF_MEDIA_INCOMING_RELEASE_LOCAL; break;
    case DF_GVS_CALL_END_PEER_HANGUP: operation = DF_MEDIA_INCOMING_RELEASE_PEER; break;
    case DF_GVS_CALL_END_SESSION_TIMEOUT: operation = DF_MEDIA_INCOMING_RELEASE_SESSION; break;
    case DF_GVS_CALL_END_RUNTIME_RESET: operation = DF_MEDIA_INCOMING_RELEASE_RUNTIME; break;
    default: break;
    }
    (void)df_runtime_media_module_release_incoming(b->ubus->media, station->id,
        b->session->generation, operation, now);
}

int df_runtime_call_hangup(struct df_runtime_call_binding *b, uint64_t generation,
    uint8_t reason, uint64_t now) {
    if (b == NULL || b->session == NULL || b->control == NULL || b->deadline == NULL ||
        b->identity == NULL || generation == 0 || generation != b->session->generation ||
        !main_active(b->session) || now < b->control->dispatch.last_now_ms ||
        now < b->control->acknowledgement.last_now_ms)
        return DF_ERR_INVALID;
    if (b->control->dispatch.command.type == DF_GVS_CALL_COMMAND_HANGUP &&
        b->control->dispatch.command.session_generation == generation && slot_busy(b->control))
        return DF_OK;
    /* Local termination supersedes this owner's pending ANSWER/CALL only. */
    cancel_commands(b, generation);
    int result = df_gvs_call_control_submit_hangup(b->control, b->session,
        generation, b->identity, reason, now);
    if (result != DF_OK) {
        bool ended;
        (void)df_gvs_session_abort(b->session, &ended);
        df_gvs_deadline_cancel(b->deadline);
        b->control->handshake.active = false;
        b->control->fault = DF_GVS_CALL_FAULT_DISPATCH_FAILED;
        b->control->terminal = (struct df_gvs_call_terminal){
            .end_reason = DF_GVS_CALL_END_LOCAL_HANGUP,
            .fault = DF_GVS_CALL_FAULT_DISPATCH_FAILED, .generation = generation};
        memcpy(b->control->terminal.station, b->session->peer, 6);
        df_runtime_call_release_ended(b, now);
    }
    return result;
}

int df_runtime_station_call_hangup(struct df_runtime_ubus *u, const char *station,
    uint64_t generation) {
    struct df_runtime_call_binding *b = binding_for(u);
    struct df_runtime_station_call_operation *op;
    const struct df_station *s;
    struct df_media_session_status_v3 media;
    int result;
    if (b == NULL || station == NULL) return DF_ERR_INVALID;
    op = &u->station_call;
    s = df_station_registry_find(b->stations, station);
    if (s == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (u->last_now_ms > UINT64_MAX - END_MS) return DF_ERR_INVALID;
    if (op->operation_id != 0 && strcmp(op->station_id, station) == 0 &&
        (generation == op->media_generation ||
         (generation == 0 && (df_runtime_station_call_active(u) || !main_active(b->session))))) {
        if (!df_runtime_station_call_active(u)) {
            if (op->cleanup_error != DF_OK)
                return finish(b, op->error, op->end_reason, op->fault, u->last_now_ms);
            return DF_OK;
        }
        if (op->stage == DF_RUNTIME_STATION_CALL_ENDING) return DF_OK;
        if (!op->command_sent)
            return finish(b, DF_OK, DF_GVS_CALL_END_LOCAL_HANGUP,
                DF_GVS_CALL_FAULT_CANCELLED, u->last_now_ms);
        if (!owner_matches(b)) return DF_ERR_INVALID;
        cancel_commands(b, op->control_generation);
        result = df_gvs_call_control_submit_hangup(b->control, b->session,
            op->control_generation, b->identity, 1, u->last_now_ms);
        if (result != DF_OK) return finish(b, result, DF_GVS_CALL_END_LOCAL_HANGUP,
            DF_GVS_CALL_FAULT_DISPATCH_FAILED, u->last_now_ms);
        op->stage = DF_RUNTIME_STATION_CALL_ENDING;
        op->deadline_ms = u->last_now_ms + END_MS;
        return DF_OK;
    }
    if (main_active(b->session) && memcmp(b->session->peer, s->logical_address, 6) == 0) {
        if (media_snapshot(u, station, &media) != DF_OK || !media.active ||
            media.purpose != DF_MEDIA_SESSION_CALL ||
            (generation != 0 && generation != media.generation)) return DF_ERR_INVALID;
        return df_runtime_call_hangup(b, b->session->generation, 1, u->last_now_ms);
    }
    return generation == 0 ? DF_OK : DF_ERR_INVALID;
}

void df_runtime_station_call_shutdown(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_call_binding *b = binding_for(u);
    if (b != NULL && df_runtime_station_call_active(u))
        (void)finish(b, DF_ERR_IO, DF_GVS_CALL_END_RUNTIME_RESET,
            DF_GVS_CALL_FAULT_RUNTIME_RESET, now);
}

void df_runtime_station_call_preempt(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_call_binding *b = binding_for(u);
    if (b != NULL && df_runtime_station_call_active(u))
        (void)finish(b, DF_MEDIA_ERROR_SESSION_PREEMPTED, DF_GVS_CALL_END_PREEMPTED,
            DF_GVS_CALL_FAULT_PREEMPTED, now);
}
