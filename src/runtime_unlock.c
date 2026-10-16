#include "runtime_unlock.h"
#include "runtime_ubus.h"
#include "runtime_service.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool active(const struct df_runtime_unlock_operation *op) {
    return op->stage == DF_RUNTIME_UNLOCK_PREPARING ||
        op->stage == DF_RUNTIME_UNLOCK_WAITING_REPLY;
}

/* Unlock admission does not require encoder/RTSP credential inspection. */
static int snapshot(struct df_runtime_ubus *u, struct df_runtime_media_status *out) {
    if (u->media == NULL || !u->media->available) return DF_ERR_IO;
    struct df_media_module_status_v3 status = {
        .sessions = u->media_session_entries, .session_count = u->media_session_capacity};
    if (df_runtime_media_module_status(u->media, &status) != DF_OK ||
        status.required_session_count > u->media_session_capacity ||
        status.session_count > u->media_session_capacity) return DF_ERR_IO;
    out->available = true;
    out->sessions = status.sessions;
    out->session_count = status.session_count;
    return DF_OK;
}

static void save(struct df_runtime_ubus *u) {
    for (size_t i = 0; i < u->unlock_record_count; ++i)
        if (strcmp(u->access_station_registry->items[i].id,
                u->unlock.station_id) == 0) {
            u->unlock_records[i] = u->unlock;
            return;
        }
}

const char *df_runtime_unlock_phase(enum df_runtime_unlock_stage stage) {
    switch (stage) {
    case DF_RUNTIME_UNLOCK_IDLE: return "idle";
    case DF_RUNTIME_UNLOCK_PREPARING: return "waiting_media";
    case DF_RUNTIME_UNLOCK_WAITING_REPLY: return "waiting_reply";
    case DF_RUNTIME_UNLOCK_COMPLETED: return "protocol_completed";
    case DF_RUNTIME_UNLOCK_REJECTED: return "protocol_rejected";
    case DF_RUNTIME_UNLOCK_PREPARATION_TIMEOUT: return "preparation_timeout";
    case DF_RUNTIME_UNLOCK_REPLY_TIMEOUT: return "expired";
    case DF_RUNTIME_UNLOCK_CANCELLED: return "cancelled";
    case DF_RUNTIME_UNLOCK_SEND_FAILED: return "send_failed";
    }
    return "unknown";
}

const char *df_runtime_unlock_mode_name(enum df_station_unlock_mode mode) {
    switch (mode) {
    case DF_STATION_UNLOCK_INHERIT: return "inherit";
    case DF_STATION_UNLOCK_DIRECT: return "direct";
    case DF_STATION_UNLOCK_DISABLED: return "disabled";
    }
    return "disabled";
}

const char *df_runtime_unlock_source_name(enum df_access_credential_source source) {
    switch (source) {
    case DF_ACCESS_CREDENTIAL_SOURCE_NONE: return "none";
    case DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL: return "global";
    case DF_ACCESS_CREDENTIAL_SOURCE_STATION: return "station";
    }
    return "none";
}

int df_runtime_ubus_bind_unlock_config(struct df_runtime_ubus *u,
    const struct df_runtime_config *config) {
    if (u == NULL || !u->started || config == NULL || u->unlock_config != NULL)
        return DF_ERR_INVALID;
    u->unlock_config = config;
    return DF_OK;
}

static int resolve(const struct df_runtime_ubus *u, const struct df_station *s,
    uint8_t material[8], struct df_runtime_unlock_station *out) {
    struct df_access_credentials credentials = {0};
    int result;
    memset(out, 0, sizeof(*out));
    memset(material, 0, 8);
    out->mode = s->unlock_mode;
    out->reason = "material_missing";
    if (!s->enabled) { out->reason = "station_unavailable"; return DF_ERR_INVALID; }
    if (s->unlock_mode == DF_STATION_UNLOCK_DISABLED) {
        out->reason = "disabled"; return DF_ERR_INVALID;
    }
    if (u->unlock_config == NULL) {
        /* Old control-only embedders retain their global material behavior. */
        out->mode = DF_STATION_UNLOCK_INHERIT;
        if (u->access == NULL || !u->access->configured) return DF_ERR_INVALID;
        memcpy(material, u->access->material, 8);
        out->credential_source = DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL;
        out->configured = true;
        out->reason = "none";
        return DF_OK;
    }
    errno = 0;
    result = df_access_credentials_load(&credentials,
        u->unlock_config->access_credentials_path);
    if (result != DF_OK && errno != ENOENT) {
        out->reason = "store_unavailable";
        df_access_credentials_destroy(&credentials);
        return DF_ERR_IO;
    }
    result = df_access_credentials_resolve(&credentials, s,
        u->unlock_config->access_material, material, &out->mode,
        &out->credential_source);
    df_access_credentials_destroy(&credentials);
    out->configured = result == DF_OK;
    if (out->configured) out->reason = "none";
    else if (s->unlock_mode == DF_STATION_UNLOCK_INHERIT &&
        u->unlock_config->access_material[0] != '\0') out->reason = "material_invalid";
    return result;
}

int df_runtime_unlock_station_metadata(const struct df_runtime_ubus *u,
    const struct df_station *s, struct df_runtime_unlock_station *out) {
    uint8_t material[8];
    int result;
    if (u == NULL || s == NULL || out == NULL) return DF_ERR_INVALID;
    result = resolve(u, s, material, out);
    memset(material, 0, sizeof(material));
    return result;
}

static int begin(struct df_runtime_ubus *u, const char *station, uint64_t now) {
    if (u == NULL || !u->started || u->access_station_registry == NULL ||
        station == NULL || station[0] == '\0' ||
        strlen(station) >= sizeof(u->unlock.station_id) || now < u->last_now_ms ||
        !df_runtime_id_is_valid(u->runtime_id)) return DF_ERR_INVALID;
    const struct df_station *s = df_station_registry_find(u->access_station_registry, station);
    struct df_runtime_unlock_station metadata;
    if (s == NULL || df_runtime_unlock_station_metadata(u, s, &metadata) != DF_OK ||
        u->next_unlock_request == UINT64_MAX || active(&u->unlock) ||
        u->unlock.cleanup_pending || now > UINT64_MAX - 30000U)
        return DF_ERR_INVALID;
    memset(&u->unlock, 0, sizeof(u->unlock));
    u->unlock.stage = DF_RUNTIME_UNLOCK_PREPARING;
    u->unlock.started_ms = now;
    u->unlock.deadline_ms = now + 30000U;
    u->unlock.effective_mode = metadata.mode;
    u->unlock.credential_source = metadata.credential_source;
    u->unlock.wait_reason = "waiting_media";
    snprintf(u->unlock.runtime_id, sizeof(u->unlock.runtime_id), "%s", u->runtime_id);
    snprintf(u->unlock.station_id, sizeof(u->unlock.station_id), "%s", station);
    snprintf(u->unlock.request_id, sizeof(u->unlock.request_id), "%016llx",
        (unsigned long long)++u->next_unlock_request);
    save(u);
    return DF_OK;
}

static int cleanup(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_unlock_operation *op = &u->unlock;
    struct df_runtime_media_status media = {0};
    const struct df_media_session_status_v3 *entry = NULL;
    int result = DF_OK;
    if (!u->manual_unlock_owns_preview || op->automatic) {
        op->cleanup_pending = false; op->cleanup_error = DF_OK; save(u); return DF_OK;
    }
    if (snapshot(u, &media) == DF_OK) {
        for (size_t i = 0; i < media.session_count; ++i)
            if (strcmp(media.sessions[i].station_id, op->station_id) == 0 &&
                media.sessions[i].generation == op->media_generation &&
                media.sessions[i].active) entry = &media.sessions[i];
        if (entry == NULL || entry->viewer_active || entry->purpose == DF_MEDIA_SESSION_CALL)
            u->manual_unlock_owns_preview = false;
    }
    if (u->manual_unlock_owns_preview) {
        struct df_media_session_key key = {op->station_id, op->media_generation};
        result = op->lightweight ?
            df_runtime_media_module_unlock_prepare_stop(u->media, &key, now) :
            df_runtime_media_module_command(u->media, DF_MEDIA_MODULE_COMMAND_STOP,
                &key, false, now);
    }
    op->cleanup_error = result;
    op->cleanup_pending = result != DF_OK;
    if (result == DF_OK) {
        u->manual_unlock_owns_preview = false;
        u->manual_unlock_station_id[0] = '\0';
    }
    save(u);
    return result;
}

static void finish(struct df_runtime_ubus *u, enum df_runtime_unlock_stage stage,
    uint64_t now) {
    if (!active(&u->unlock)) return;
    uint64_t floor = u->unlock.has_sent ? u->unlock.sent_ms : u->unlock.started_ms;
    if (now < floor) now = floor;
    u->unlock.stage = stage;
    u->unlock.ended_ms = now;
    u->unlock.deadline_ms = 0;
    u->unlock.wait_reason = "none";
    u->manual_unlock_pending = false;
    u->manual_unlock_inflight = false;
    if (u->unlock.automatic && stage == DF_RUNTIME_UNLOCK_CANCELLED &&
        u->auto_unlock_access_generation == u->unlock.access_generation)
        u->auto_unlock_inflight = false;
    save(u);
}

void df_runtime_unlock_cancel(struct df_runtime_ubus *u, uint64_t now) {
    if (u == NULL) return;
    finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now);
    (void)cleanup(u, now);
}

int df_runtime_unlock_send(struct df_runtime_ubus *u, const char *station_id,
    uint64_t now) {
    const struct df_station *s;
    struct df_runtime_unlock_station metadata;
    uint8_t material[8];
    int result;
    if (u == NULL || !u->started || !u->active_host || u->access == NULL ||
        station_id == NULL || u->access_identity == NULL ||
        u->station_access_session == NULL ||
        u->access->result.state == DF_GVS_ACCESS_PROTOCOL_WAITING ||
        u->station_access_session->generation == UINT64_MAX ||
        u->unlock.stage != DF_RUNTIME_UNLOCK_PREPARING ||
        strcmp(u->unlock.runtime_id, u->runtime_id) != 0 ||
        strcmp(u->unlock.station_id, station_id) != 0) return DF_ERR_INVALID;
    s = df_station_registry_find(u->access_station_registry, station_id);
    if (s == NULL || resolve(u, s, material, &metadata) != DF_OK) {
        u->unlock.error = DF_ERR_INVALID;
        finish(u, DF_RUNTIME_UNLOCK_SEND_FAILED, now);
        return DF_ERR_INVALID;
    }
    /* The protocol generation is independent from the public media generation. */
    uint64_t generation = u->station_access_session->generation + 1U;
    memset(u->station_access_session, 0, sizeof(*u->station_access_session));
    u->station_access_session->state = DF_GVS_PREVIEW;
    u->station_access_session->generation = generation;
    memcpy(u->station_access_session->peer, s->logical_address, 6);
    u->station_access_active = true;
    result = df_gvs_access_control_submit_material(u->access,
        u->station_access_session, generation, u->access_identity, material, now);
    memset(material, 0, sizeof(material));
    u->unlock.effective_mode = metadata.mode;
    u->unlock.credential_source = metadata.credential_source;
    u->unlock.access_generation = generation;
    u->unlock.error = result;
    if (result != DF_OK) finish(u, DF_RUNTIME_UNLOCK_SEND_FAILED, now);
    else {
        u->unlock.has_sent = true;
        u->unlock.sent_ms = now;
        u->unlock.stage = DF_RUNTIME_UNLOCK_WAITING_REPLY;
        u->unlock.deadline_ms = now + 1000U;
        u->unlock.wait_reason = "none";
        if (!u->unlock.automatic) {
            u->manual_unlock_pending = false;
            u->manual_unlock_inflight = true;
            u->manual_unlock_access_generation = generation;
        }
    }
    save(u);
    return result;
}

int df_runtime_unlock_begin_auto(struct df_runtime_ubus *u, const char *station,
    uint64_t generation, uint64_t now) {
    int result = begin(u, station, now);
    if (result != DF_OK) return result;
    u->unlock.automatic = true;
    u->unlock.reused_session = true;
    u->unlock.media_generation = generation;
    save(u);
    return DF_OK;
}

void df_runtime_unlock_observe_result(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_unlock_operation *op;
    const struct df_gvs_access_result *r;
    enum df_runtime_unlock_stage stage;
    const struct df_station *s;
    if (u == NULL || u->access == NULL || u->access_identity == NULL) return;
    op = &u->unlock;
    if (op->stage != DF_RUNTIME_UNLOCK_WAITING_REPLY) return;
    if (now < op->sent_ms) return;
    struct df_runtime_media_status media = {0};
    if (op->automatic && u->media == NULL) {
        /* Legacy control-only fixtures have no public media owner snapshot. */
    } else if (snapshot(u, &media) != DF_OK) {
        if (now >= op->deadline_ms) finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now);
        return;
    }
    bool media_matches = op->automatic && u->media == NULL;
    for (size_t i = 0; i < media.session_count; ++i)
        if (media.sessions[i].active && media.sessions[i].generation == op->media_generation &&
            strcmp(media.sessions[i].station_id, op->station_id) == 0) media_matches = true;
    if (!media_matches) { finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now); return; }
    if (op->lightweight && op->preparation_generation != 0) {
        struct df_media_unlock_prepare_status_v1 status = {0};
        if (df_runtime_media_module_unlock_prepare_status(u->media, op->station_id,
                op->media_generation, &status) == DF_OK &&
            status.generation == op->media_generation &&
            status.attempt_generation == op->preparation_generation &&
            status.has_confirmed && status.confirmed_ms >= op->started_ms) {
            op->has_confirmed = true;
            op->confirmed_ms = status.confirmed_ms;
            save(u);
        }
    }
    r = &u->access->result;
    s = df_station_registry_find(u->access_station_registry, op->station_id);
    if (s == NULL || strcmp(op->runtime_id, u->runtime_id) != 0 ||
        r->session_generation != op->access_generation ||
        memcmp(r->destination, s->logical_address, 6) != 0 ||
        memcmp(r->source, u->access_identity, 6) != 0) {
        finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now); return;
    }
    switch (r->state) {
    case DF_GVS_ACCESS_PROTOCOL_COMPLETED: stage = DF_RUNTIME_UNLOCK_COMPLETED; break;
    case DF_GVS_ACCESS_PROTOCOL_REJECTED: stage = DF_RUNTIME_UNLOCK_REJECTED; break;
    case DF_GVS_ACCESS_PROTOCOL_EXPIRED: stage = DF_RUNTIME_UNLOCK_REPLY_TIMEOUT; break;
    case DF_GVS_ACCESS_PROTOCOL_CANCELLED: stage = DF_RUNTIME_UNLOCK_CANCELLED; break;
    case DF_GVS_ACCESS_PROTOCOL_SEND_FAILED: stage = DF_RUNTIME_UNLOCK_SEND_FAILED; break;
    default:
        if (now < op->deadline_ms) return;
        stage = DF_RUNTIME_UNLOCK_REPLY_TIMEOUT;
    }
    if (stage == DF_RUNTIME_UNLOCK_COMPLETED || stage == DF_RUNTIME_UNLOCK_REJECTED) {
        /* 04/89 attribution is only the current local address/time window. */
        if (r->reply_ms < op->sent_ms || r->reply_ms >= op->deadline_ms) {
            finish(u, DF_RUNTIME_UNLOCK_REPLY_TIMEOUT, now); return;
        }
        op->has_reply = true;
        op->reply_ms = r->reply_ms;
        op->raw_status_valid = true;
        op->raw_status = r->raw_status;
    }
    finish(u, stage, op->has_reply ? op->reply_ms : now);
}

int df_runtime_ubus_unlock_station(struct df_runtime_ubus *u,
    const char *station, uint64_t now) {
    struct df_runtime_media_status media = {0};
    uint64_t generation = 0;
    bool reused = false;
    int result;
    if (u == NULL || !u->started || !u->active_host || station == NULL ||
        station[0] == '\0' || strlen(station) >= sizeof(u->manual_unlock_station_id) ||
        u->access == NULL || u->station_access_session == NULL ||
        u->access_station_registry == NULL || now < u->last_now_ms)
        return DF_ERR_INVALID;
    if (active(&u->unlock))
        return !u->unlock.automatic && strcmp(u->unlock.station_id, station) == 0 &&
            strcmp(u->unlock.runtime_id, u->runtime_id) == 0 ? DF_OK : DF_ERR_INVALID;
    if (u->auto_unlock_pending || u->auto_unlock_inflight ||
        u->access->result.state == DF_GVS_ACCESS_PROTOCOL_WAITING ||
        u->unlock.cleanup_pending) return DF_ERR_INVALID;
    if (u->media == NULL || u->media->unlock_prepare_invalid ||
        snapshot(u, &media) != DF_OK || !media.available)
        return DF_ERR_IO;
    for (size_t i = 0; i < media.session_count; ++i) {
        const struct df_media_session_status_v3 *entry = &media.sessions[i];
        if (entry->active && strcmp(entry->station_id, station) == 0) {
            if (entry->state == DF_MEDIA_SESSION_FAILED ||
                entry->state == DF_MEDIA_SESSION_STOPPING ||
                entry->state == DF_MEDIA_SESSION_PREEMPTED) return DF_ERR_INVALID;
            generation = entry->generation;
            reused = true;
            break;
        }
    }
    if (generation == 0 && u->access_session != NULL &&
        (u->access_session->state == DF_GVS_RINGING ||
         u->access_session->state == DF_GVS_TALKING)) return DF_ERR_INVALID;
    result = begin(u, station, now);
    if (result != DF_OK) return result;
    u->unlock.reused_session = reused;
    u->unlock.lightweight = !reused && u->media->unlock_prepare_api != NULL;
    if (!reused) {
        result = u->unlock.lightweight ?
            df_runtime_media_module_unlock_prepare_start(u->media, station, now, &generation) :
            df_runtime_media_module_request_start(u->media, station, now, &generation);
        if (result != DF_OK || generation == 0) {
            u->unlock.error = result;
            finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now);
            return DF_OK;
        }
    }
    u->unlock.media_generation = generation;
    snprintf(u->manual_unlock_station_id, sizeof(u->manual_unlock_station_id), "%s", station);
    u->manual_unlock_media_generation = generation;
    u->manual_unlock_owns_preview = !reused;
    u->manual_unlock_deadline_ms = u->unlock.deadline_ms;
    u->manual_unlock_pending = true;
    save(u);
    (void)df_runtime_ubus_tick_manual_unlock(u, now);
    return DF_OK;
}

int df_runtime_ubus_tick_manual_unlock(struct df_runtime_ubus *u, uint64_t now) {
    struct df_runtime_unlock_operation *op;
    struct df_runtime_media_status media = {0};
    const struct df_media_session_status_v3 *entry = NULL;
    bool ready = false, full_media = false;
    if (u == NULL || now < u->last_now_ms) return DF_ERR_INVALID;
    op = &u->unlock;
    full_media = !op->lightweight;
    df_runtime_unlock_observe_result(u, now);
    if (op->automatic) return DF_OK;
    if (!active(op)) return cleanup(u, now);
    if (strcmp(op->runtime_id, u->runtime_id) != 0 || !u->active_host ||
        (u->auto_unlock_pending && op->stage == DF_RUNTIME_UNLOCK_PREPARING)) {
        finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now); return cleanup(u, now);
    }
    if (op->stage == DF_RUNTIME_UNLOCK_WAITING_REPLY) return DF_OK;
    if (now >= op->deadline_ms) {
        finish(u, DF_RUNTIME_UNLOCK_PREPARATION_TIMEOUT, now); return cleanup(u, now);
    }
    if (snapshot(u, &media) != DF_OK) return DF_OK;
    for (size_t i = 0; i < media.session_count; ++i)
        if (media.sessions[i].active && media.sessions[i].generation == op->media_generation &&
            strcmp(media.sessions[i].station_id, op->station_id) == 0) entry = &media.sessions[i];
    if (entry == NULL || entry->state == DF_MEDIA_SESSION_FAILED ||
        entry->state == DF_MEDIA_SESSION_PREEMPTED) {
        finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now); return cleanup(u, now);
    }
    if (entry->viewer_active || entry->purpose == DF_MEDIA_SESSION_CALL)
        u->manual_unlock_owns_preview = false;
    if (op->lightweight) {
        struct df_media_unlock_prepare_status_v1 status = {0};
        int result = df_runtime_media_module_unlock_prepare_status(u->media,
            op->station_id, op->media_generation, &status);
        if (result != DF_OK) { op->error = result; save(u); return DF_OK; }
        if (status.generation != op->media_generation) {
            finish(u, DF_RUNTIME_UNLOCK_CANCELLED, now); return cleanup(u, now);
        }
        op->attempts = status.attempts;
        op->preparation_generation = status.attempt_generation;
        op->busy_replies = status.busy_replies;
        op->wait_reason = status.cooldown ? "cooldown" : status.busy ? "busy" : "waiting_media";
        op->has_first_request = status.has_first_request && status.first_request_ms >= op->started_ms;
        op->first_request_ms = status.first_request_ms;
        op->has_confirmed = status.has_confirmed && status.confirmed_ms >= op->started_ms;
        op->confirmed_ms = status.confirmed_ms;
        op->has_first_media = status.current_attempt && status.first_frame_ms >= op->started_ms &&
            status.ready && status.source_live;
        op->first_media_ms = status.first_frame_ms;
        ready = status.active && status.ready && status.source_live && status.current_attempt;
        /* Only our temporary preparation can use the current monitor ACK.
         * ready/current_attempt still mean validated JPEG, not control success.
         * Reused/adopted sessions retain their existing real-media gate. */
        if (u->manual_unlock_owns_preview && status.active &&
            entry->purpose == DF_MEDIA_SESSION_PREVIEW &&
            entry->state == DF_MEDIA_SESSION_AWAITING_VIDEO &&
            status.attempt_generation != 0 && !status.busy && !status.cooldown &&
            op->has_first_request && op->has_confirmed &&
            status.confirmed_ms >= status.first_request_ms && status.confirmed_ms <= now)
            ready = true;
        /* A viewer/call can adopt the same preparation generation. */
        if (!status.active && !u->manual_unlock_owns_preview) full_media = true;
    }
    if (full_media) {
        ready = entry->ready && entry->source_live &&
            (entry->state == DF_MEDIA_SESSION_PUBLISHING || entry->state == DF_MEDIA_SESSION_VIEWING);
        op->wait_reason = "waiting_media";
    }
    save(u);
    if (!ready) return DF_OK;
    int result = df_runtime_unlock_send(u, op->station_id, now);
    if (result != DF_OK) (void)cleanup(u, now);
    return result;
}

bool df_runtime_unlock_operation_valid(const struct df_runtime_unlock_operation *op) {
    if (op == NULL || op->stage <= DF_RUNTIME_UNLOCK_IDLE ||
        op->stage > DF_RUNTIME_UNLOCK_SEND_FAILED || !df_runtime_id_is_valid(op->runtime_id) ||
        op->request_id[16] != '\0' || op->station_id[0] < 'a' || op->station_id[0] > 'z' ||
        op->station_id[sizeof(op->station_id) - 1] != '\0' ||
        strspn(op->request_id, "0123456789abcdef") != 16 ||
        strcmp(op->request_id, "0000000000000000") == 0 ||
        op->effective_mode < DF_STATION_UNLOCK_INHERIT ||
        op->effective_mode > DF_STATION_UNLOCK_DISABLED ||
        op->credential_source < DF_ACCESS_CREDENTIAL_SOURCE_NONE ||
        op->credential_source > DF_ACCESS_CREDENTIAL_SOURCE_STATION) return false;
    return strspn(op->station_id, "abcdefghijklmnopqrstuvwxyz0123456789_") == strlen(op->station_id);
}

void df_runtime_unlock_emit_operation(const struct df_runtime_unlock_operation *op,
    uint64_t now, const struct df_runtime_unlock_sink *sink) {
    if (!df_runtime_unlock_operation_valid(op) || sink == NULL ||
        sink->string == NULL || sink->number == NULL || sink->boolean == NULL) return;
    void *c = sink->context;
    sink->string(c, "station_id", op->station_id);
    sink->string(c, "runtime_id", op->runtime_id);
    sink->string(c, "request_id", op->request_id);
    sink->string(c, "phase", df_runtime_unlock_phase(op->stage));
    static const char *const reasons[] = {"none", "busy", "cooldown", "waiting_media",
        "compatibility", "station_unavailable", "unknown"};
    const char *reason = "unknown";
    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
        if (op->wait_reason != NULL && strcmp(op->wait_reason, reasons[i]) == 0) reason = reasons[i];
    sink->string(c, "wait_reason", reason);
    sink->string(c, "preparation_path", op->reused_session ? "reused" :
        op->lightweight ? "lightweight" : "full_media");
    sink->string(c, "mode", df_runtime_unlock_mode_name(op->effective_mode));
    sink->string(c, "credential_source", df_runtime_unlock_source_name(op->credential_source));
    sink->boolean(c, "reused_session", op->reused_session);
    sink->boolean(c, "physical_result_confirmed", false);
    sink->number(c, "attempts", op->attempts);
    sink->number(c, "busy_replies", op->busy_replies);
    sink->number(c, "remaining_ms", active(op) && now < op->deadline_ms ? op->deadline_ms - now : 0);
    uint64_t end = active(op) ? now : op->ended_ms;
    sink->number(c, "elapsed_ms", end >= op->started_ms ? end - op->started_ms : 0);
    if (op->has_first_request && op->first_request_ms >= op->started_ms)
        sink->number(c, "first_request_ms", op->first_request_ms - op->started_ms);
    if (op->has_confirmed && op->confirmed_ms >= op->started_ms)
        sink->number(c, "confirmed_ms", op->confirmed_ms - op->started_ms);
    if (op->has_first_media && op->first_media_ms >= op->started_ms)
        sink->number(c, "first_media_ms", op->first_media_ms - op->started_ms);
    if (op->has_sent && op->sent_ms >= op->started_ms) {
        sink->number(c, "sent_ms", op->sent_ms - op->started_ms);
        sink->number(c, "preparation_ms", op->sent_ms - op->started_ms);
    } else if (!active(op) && end >= op->started_ms)
        sink->number(c, "preparation_ms", end - op->started_ms);
    if (op->has_sent && op->has_reply && op->reply_ms >= op->sent_ms) {
        sink->number(c, "reply_ms", op->reply_ms - op->started_ms);
        sink->number(c, "reply_latency_ms", op->reply_ms - op->sent_ms);
    }
    if (op->raw_status_valid && (op->stage == DF_RUNTIME_UNLOCK_COMPLETED ||
        op->stage == DF_RUNTIME_UNLOCK_REJECTED)) sink->number(c, "raw_status", op->raw_status);
    sink->string(c, "error", op->error == DF_OK ? "none" : "unknown");
    sink->string(c, "cleanup_error", op->cleanup_error == DF_OK ? "none" : "cleanup_failed");
}
