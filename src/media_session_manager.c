#include "media_session_manager.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doorfast.h"

#define DF_MEDIA_SESSION_FRAME_STALL_TIMEOUT_MS 5000U

const char *df_media_video_reject_reason_name(
    enum df_media_video_reject_reason reason) {
    switch (reason) {
    case DF_MEDIA_VIDEO_REJECT_DESTINATION: return "destination_mismatch";
    case DF_MEDIA_VIDEO_REJECT_SOURCE_IPV4: return "source_ipv4_mismatch";
    case DF_MEDIA_VIDEO_REJECT_SESSION: return "session_not_found";
    case DF_MEDIA_VIDEO_REJECT_MONITOR: return "monitor_mismatch";
    case DF_MEDIA_VIDEO_REJECT_JPEG: return "jpeg_invalid";
    case DF_MEDIA_VIDEO_REJECT_DIMENSIONS: return "jpeg_dimensions_invalid";
    case DF_MEDIA_VIDEO_REJECT_ENCODER: return "encoder_failed";
    }
    return "unknown";
}

static void df_media_video_reject_log(const char *reason,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4) {
    static unsigned diagnostic_logs;

    if (diagnostic_logs >= 64U) return;
    (void)fprintf(stderr,
        "doorfast: event=media_video_rejected reason=%s source_ipv4=%u"
        " frame=%u full=%u count=%u index=%u chunk=%u capacity=%u\n",
        reason == NULL ? "unknown" : reason, (unsigned)source_ipv4,
        packet == NULL ? 0U : (unsigned)packet->frame_no,
        packet == NULL ? 0U : (unsigned)packet->full_length,
        packet == NULL ? 0U : (unsigned)packet->chunk_count,
        packet == NULL ? 0U : (unsigned)packet->chunk_index,
        packet == NULL ? 0U : (unsigned)packet->chunk_length,
        packet == NULL ? 0U : (unsigned)packet->capacity);
    diagnostic_logs++;
}

static const char *df_media_monitor_reject_name(
    enum df_gvs_monitor_admit_reject reason) {
    switch (reason) {
    case DF_GVS_MONITOR_ADMIT_REJECT_CLOCK: return "clock";
    case DF_GVS_MONITOR_ADMIT_REJECT_GENERATION: return "generation";
    case DF_GVS_MONITOR_ADMIT_REJECT_SOURCE_IPV4: return "source_ipv4";
    case DF_GVS_MONITOR_ADMIT_REJECT_STATE: return "state";
    case DF_GVS_MONITOR_ADMIT_REJECT_SOURCE: return "source";
    case DF_GVS_MONITOR_ADMIT_REJECT_DESTINATION: return "destination";
    case DF_GVS_MONITOR_ADMIT_REJECT_NONE: break;
    }
    return "unknown";
}

static void df_media_monitor_reject_log(
    const struct df_media_session *session,
    const uint8_t source[6], const uint8_t destination[6],
    uint32_t source_ipv4, uint64_t timestamp_ms,
    const struct df_gvs_monitor_result *result) {
    static unsigned diagnostic_logs;

    if (diagnostic_logs >= 64U || session == NULL || result == NULL) return;
    (void)fprintf(stderr,
        "doorfast: event=media_monitor_rejected station_id=%s "
        "reason=%s generation=%llu monitor_generation=%llu state=%d "
        "timestamp_ms=%llu last_now_ms=%llu source_ipv4=%u "
        "source=%02x:%02x:%02x:%02x:%02x:%02x "
        "expected_source=%02x:%02x:%02x:%02x:%02x:%02x "
        "destination=%02x:%02x:%02x:%02x:%02x:%02x "
        "expected_destination=%02x:%02x:%02x:%02x:%02x:%02x\n",
        session->station_id, df_media_monitor_reject_name(result->admit_reject),
        (unsigned long long)session->generation,
        (unsigned long long)session->monitor.generation,
        (int)session->monitor.state, (unsigned long long)timestamp_ms,
        (unsigned long long)session->monitor.last_now_ms,
        (unsigned)source_ipv4,
        source == NULL ? 0U : source[0], source == NULL ? 0U : source[1],
        source == NULL ? 0U : source[2], source == NULL ? 0U : source[3],
        source == NULL ? 0U : source[4], source == NULL ? 0U : source[5],
        session->station[0], session->station[1], session->station[2],
        session->station[3], session->station[4], session->station[5],
        destination == NULL ? 0U : destination[0],
        destination == NULL ? 0U : destination[1],
        destination == NULL ? 0U : destination[2],
        destination == NULL ? 0U : destination[3],
        destination == NULL ? 0U : destination[4],
        destination == NULL ? 0U : destination[5],
        session->monitor.local[0], session->monitor.local[1],
        session->monitor.local[2], session->monitor.local[3],
        session->monitor.local[4], session->monitor.local[5]);
    diagnostic_logs++;
}

static void df_media_session_manager_advance_revisions(
    struct df_media_session_manager *manager,
    struct df_media_session *session) {
    if (manager != NULL && manager->status_revision != UINT64_MAX)
        manager->status_revision++;
    if (session != NULL && session->status_revision != UINT64_MAX)
        session->status_revision++;
    if (manager != NULL) manager->status_initialized = false;
    if (session != NULL) session->status_initialized = false;
}

static int df_media_session_resource_start_default(
    struct df_media_session *session, uint64_t proposed_generation,
    void *context) {
    (void)session;
    (void)proposed_generation;
    (void)context;
    return DF_OK;
}

static void df_media_session_resource_stop_default(
    struct df_media_session *session, void *context) {
    (void)session;
    (void)context;
}

static int df_media_session_manager_stop_resources(
    struct df_media_session *session) {
    int result;

    if (session == NULL) return DF_ERR_INVALID;
    result = df_media_session_stop_pipeline(session);
    if (session->stop_resources != NULL) {
        session->stop_resources(session, session->resource_context);
        session->stop_resources = NULL;
        session->resource_context = NULL;
    }
    return result;
}

static int df_media_session_manager_emit_stop_best_effort(
    struct df_media_session_manager *manager,
    struct df_media_session *session, uint64_t now_ms) {
    struct df_gvs_monitor_action action;

    if (manager == NULL || session == NULL) return DF_ERR_INVALID;
    if (session->monitor.state == DF_GVS_MONITOR_IDLE) return DF_OK;
    if (df_gvs_monitor_stop(&session->monitor, session->generation,
            now_ms) != DF_OK ||
        df_gvs_monitor_step(&session->monitor, now_ms, &action) != DF_OK)
        return DF_ERR_IO;
    if (action.send && manager->callbacks.emit_control(action.destination,
            session->station_ipv4, action.source, action.family,
            action.opcode, action.payload, action.payload_length,
            manager->callbacks.context) != DF_OK)
        return DF_ERR_IO;
    return DF_OK;
}

static int df_media_session_manager_release_failed(
    struct df_media_session_manager *manager,
    struct df_media_session *session) {
    if (manager == NULL || session == NULL || !session->active)
        return DF_ERR_INVALID;
    if (session->last_error != DF_MEDIA_ERROR_NONE) {
        (void)snprintf(manager->failed_station_id,
            sizeof(manager->failed_station_id), "%s", session->station_id);
        manager->failed_generation = session->generation;
        manager->failed_error = session->last_error;
    }
    if (df_media_session_manager_stop_resources(session) != DF_OK)
        return DF_ERR_IO;
    session->active = false;
    session->reserved = false;
    if (manager->active_count > 0U) manager->active_count--;
    return DF_OK;
}

static char *df_media_session_manager_copy_string(const char *source) {
    size_t length;
    char *copy;

    if (source == NULL) return NULL;
    length = strlen(source) + 1U;
    copy = malloc(length);
    if (copy != NULL) memcpy(copy, source, length);
    return copy;
}

static void df_media_session_manager_free_config(
    struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return;
    if (manager->stations != NULL) {
        for (index = 0U; index < manager->config.station_count; index++) {
            free((void *)manager->stations[index].id);
            free((void *)manager->stations[index].stream_name);
        }
    }
    free(manager->stations);
    free(manager->go2rtc_host);
    free(manager->rtsp_username);
    free(manager->credentials_path);
    manager->stations = NULL;
    manager->go2rtc_host = NULL;
    manager->rtsp_username = NULL;
    manager->credentials_path = NULL;
}

static int df_media_session_manager_copy_config(
    struct df_media_session_manager *manager,
    const struct df_media_module_config_v3 *config) {
    size_t index;

    manager->config = *config;
    if (config->station_count > 0U) {
        manager->stations = calloc(config->station_count,
            sizeof(*manager->stations));
        if (manager->stations == NULL) return DF_ERR_IO;
    }
    manager->config.stations = manager->stations;
    for (index = 0U; index < config->station_count; index++) {
        manager->stations[index] = config->stations[index];
        manager->stations[index].id =
            df_media_session_manager_copy_string(config->stations[index].id);
        manager->stations[index].stream_name =
            df_media_session_manager_copy_string(
                config->stations[index].stream_name);
        if (manager->stations[index].id == NULL ||
            manager->stations[index].stream_name == NULL) return DF_ERR_IO;
    }

    manager->go2rtc_host =
        df_media_session_manager_copy_string(config->go2rtc_host);
    manager->rtsp_username =
        df_media_session_manager_copy_string(config->rtsp_username);
    manager->credentials_path =
        df_media_session_manager_copy_string(config->credentials_path);
    if ((config->go2rtc_host != NULL && manager->go2rtc_host == NULL) ||
        (config->rtsp_username != NULL && manager->rtsp_username == NULL) ||
        (config->credentials_path != NULL && manager->credentials_path == NULL))
        return DF_ERR_IO;
    manager->config.go2rtc_host = manager->go2rtc_host;
    manager->config.rtsp_username = manager->rtsp_username;
    manager->config.credentials_path = manager->credentials_path;
    return DF_OK;
}

static const struct df_media_station_config_v3 *
df_media_session_manager_find_station(
    const struct df_media_session_manager *manager, const char *station_id) {
    size_t index;

    if (manager == NULL || station_id == NULL) return NULL;
    for (index = 0U; index < manager->config.station_count; index++) {
        if (strcmp(manager->stations[index].id, station_id) == 0)
            return &manager->stations[index];
    }
    return NULL;
}

static struct df_media_session *df_media_session_manager_find_active(
    struct df_media_session_manager *manager, const char *station_id) {
    size_t index;

    if (manager == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (df_media_session_matches_station(&manager->sessions[index],
                station_id)) return &manager->sessions[index];
    }
    return NULL;
}

static struct df_media_session *df_media_session_manager_find_free(
    struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (!manager->sessions[index].active &&
            !manager->sessions[index].reserved)
            return &manager->sessions[index];
    }
    return NULL;
}

static const struct df_media_session *df_media_session_manager_find_const(
    const struct df_media_session_manager *manager, const char *station_id) {
    size_t index;

    if (manager == NULL || station_id == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (df_media_session_matches_station(&manager->sessions[index],
                station_id)) return &manager->sessions[index];
    }
    return NULL;
}

const struct df_media_session *df_media_session_manager_find(
    const struct df_media_session_manager *manager, const char *station_id) {
    return df_media_session_manager_find_const(manager, station_id);
}

static int df_media_session_manager_start_monitor(
    struct df_media_session_manager *manager,
    struct df_media_session *session, uint64_t now_ms) {
    if (manager->config.local[0] == 0U) return DF_OK;
    if (manager->config.first_frame_timeout_s != 0U &&
        df_gvs_monitor_set_first_frame_timeout(&session->monitor,
            (uint64_t)manager->config.first_frame_timeout_s * 1000U) != DF_OK)
        return DF_ERR_INVALID;
    return df_gvs_monitor_start_with_generation(&session->monitor,
        manager->config.local, session->station, session->station_ipv4,
        session->generation, now_ms);
}

static int df_media_session_manager_bind_call_monitor(
    struct df_media_session_manager *manager,
    struct df_media_session *session, uint64_t now_ms) {
    if (manager->config.local[0] == 0U) return DF_OK;
    if (manager->config.first_frame_timeout_s != 0U)
        session->monitor.first_frame_timeout_ms =
            (uint64_t)manager->config.first_frame_timeout_s * 1000U;
    return df_gvs_monitor_bind_call(&session->monitor, manager->config.local,
        session->station, session->station_ipv4, session->generation, now_ms);
}

static int df_media_session_manager_call_monitor_preflight(
    const struct df_media_session_manager *manager,
    const struct df_media_session *session, uint64_t now_ms) {
    uint64_t timeout_ms;

    if (manager->config.local[0] == 0U) return DF_OK;
    timeout_ms = manager->config.first_frame_timeout_s == 0U ?
        session->monitor.first_frame_timeout_ms :
        (uint64_t)manager->config.first_frame_timeout_s * 1000U;
    if (timeout_ms == 0U) timeout_ms = DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS;
    if (now_ms < session->monitor.last_now_ms ||
        now_ms > UINT64_MAX - timeout_ms) return DF_ERR_INVALID;
    return DF_OK;
}

int df_media_session_manager_init(struct df_media_session_manager *manager,
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    size_t enabled_count = 0U;
    size_t requested;
    size_t index;

    if (manager == NULL || manager->initialized) return DF_ERR_INVALID;
    memset(manager, 0, sizeof(*manager));
    if (df_media_module_config_v3_validate(config, callbacks) != DF_OK)
        return DF_ERR_INVALID;
    if (df_media_session_manager_copy_config(manager, config) != DF_OK) {
        df_media_session_manager_free_config(manager);
        memset(manager, 0, sizeof(*manager));
        return DF_ERR_IO;
    }
    if (manager->config.credentials_path != NULL &&
        df_media_credentials_load(manager->config.credentials_path,
            &manager->credentials) != DF_OK) {
        df_media_session_manager_free_config(manager);
        memset(manager, 0, sizeof(*manager));
        return DF_ERR_INVALID;
    }
    manager->callbacks = *callbacks;
    for (index = 0U; index < config->station_count; index++) {
        if (config->stations[index].enabled) enabled_count++;
    }
    requested = config->max_encoders == 0U ? 1U : config->max_encoders;
    manager->capacity = config->enabled && requested < enabled_count ?
        requested : (config->enabled ? enabled_count : 0U);
    if (manager->capacity > 0U) {
        manager->sessions = calloc(manager->capacity,
            sizeof(*manager->sessions));
        if (manager->sessions == NULL) {
            df_media_session_manager_free_config(manager);
            memset(manager, 0, sizeof(*manager));
            return DF_ERR_IO;
        }
    }
    manager->resource_hooks.start =
        df_media_session_resource_start_default;
    manager->resource_hooks.stop = df_media_session_resource_stop_default;
    manager->initialized = true;
    return DF_OK;
}

void df_media_session_manager_destroy(struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return;
    if (manager->sessions != NULL) {
        for (index = 0U; index < manager->capacity; index++) {
            if (manager->sessions[index].active ||
                manager->sessions[index].reserved)
                (void)df_media_session_manager_stop_resources(
                    &manager->sessions[index]);
            df_media_session_reset(&manager->sessions[index]);
        }
    }
    free(manager->sessions);
    df_media_session_manager_free_config(manager);
    memset(manager, 0, sizeof(*manager));
}

void df_media_session_manager_set_resource_hooks(
    struct df_media_session_manager *manager,
    const struct df_media_session_resource_hooks *hooks) {
    if (manager == NULL || !manager->initialized) return;
    if (hooks == NULL) {
        manager->resource_hooks.start =
            df_media_session_resource_start_default;
        manager->resource_hooks.stop = df_media_session_resource_stop_default;
        manager->resource_hooks.context = NULL;
        return;
    }
    manager->resource_hooks.start = hooks->start == NULL ?
        df_media_session_resource_start_default : hooks->start;
    manager->resource_hooks.stop = hooks->stop == NULL ?
        df_media_session_resource_stop_default : hooks->stop;
    manager->resource_hooks.context = hooks->context;
}

int df_media_session_manager_start(struct df_media_session_manager *manager,
    const char *station_id, enum df_media_session_purpose purpose,
    uint64_t now_ms, uint64_t *generation) {
    const struct df_media_station_config_v3 *station;
    struct df_media_session_resource_hooks resource_hooks;
    struct df_media_session *session;
    uint64_t available_kib;
    uint64_t proposed_generation;
    uint32_t station_ipv4;
    int result;

    if (generation == NULL) return DF_ERR_INVALID;
    *generation = 0U;
    if (manager == NULL || !manager->initialized || station_id == NULL ||
        station_id[0] == '\0' ||
        (purpose != DF_MEDIA_SESSION_PREVIEW &&
         purpose != DF_MEDIA_SESSION_CALL)) return DF_ERR_INVALID;

    station = df_media_session_manager_find_station(manager, station_id);
    if (station == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!manager->config.enabled || !station->enabled)
        return DF_MEDIA_ERROR_STATION_DISABLED;
    session = df_media_session_manager_find_active(manager, station_id);
    if (session != NULL) {
        *generation = session->generation;
        return DF_OK;
    }
    if (manager->active_count >= manager->capacity)
        return DF_MEDIA_ERROR_CAPACITY_BUSY;
    session = df_media_session_manager_find_free(manager);
    if (session == NULL) return DF_MEDIA_ERROR_CAPACITY_BUSY;

    station_ipv4 = station->ipv4;
    if (station_ipv4 == 0U &&
        (manager->callbacks.resolve_route(station->logical_address, now_ms,
             &station_ipv4, manager->callbacks.context) != DF_OK ||
         station_ipv4 == 0U)) return DF_MEDIA_ERROR_ROUTE_UNAVAILABLE;
    if (manager->config.min_free_kib != 0U &&
        (manager->callbacks.available_memory(&available_kib,
             manager->callbacks.context) != DF_OK ||
         available_kib < manager->config.min_free_kib))
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;
    if (manager->next_generation == UINT64_MAX)
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;

    result = df_media_session_prepare(session, station, station_ipv4, purpose,
        now_ms);
    if (result != DF_OK) return DF_ERR_INVALID;
    proposed_generation = manager->next_generation + 1U;
    resource_hooks = manager->resource_hooks;
    session->stop_resources = resource_hooks.stop;
    session->resource_context = resource_hooks.context;
    if (resource_hooks.start(session, proposed_generation,
            resource_hooks.context) != DF_OK) {
        (void)df_media_session_manager_stop_resources(session);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    if (df_media_session_activate(session, proposed_generation) != DF_OK) {
        (void)df_media_session_manager_stop_resources(session);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    if (df_media_session_manager_start_monitor(manager, session, now_ms) != DF_OK) {
        df_media_session_manager_stop_resources(session);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    manager->next_generation = proposed_generation;
    manager->active_count++;
    *generation = proposed_generation;
    df_media_session_manager_advance_revisions(manager, session);
    return DF_OK;
}

int df_media_session_manager_receive_control(
    struct df_media_session_manager *manager, const struct df_gvs_frame *frame,
    uint32_t source_ipv4, uint64_t now_ms) {
    size_t index;

    if (manager == NULL || !manager->initialized || frame == NULL)
        return DF_ERR_INVALID;
    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];
        struct df_gvs_monitor_result result;
        struct df_gvs_monitor previous_monitor;

        if (!session->active || memcmp(session->station, frame->source,
                sizeof(session->station)) != 0 ||
            source_ipv4 != session->station_ipv4)
            continue;
        if (memcmp(frame->destination, manager->config.local,
                sizeof(manager->config.local)) != 0)
            return DF_ERR_INVALID;
        if (frame->family == 0x03U && frame->opcode == 0x51U &&
            session->purpose != DF_MEDIA_SESSION_PREVIEW)
            return DF_ERR_INVALID;
        if (frame->family == 0x03U && frame->opcode == 0x02U &&
            session->purpose != DF_MEDIA_SESSION_PREVIEW)
            return DF_ERR_INVALID;
        previous_monitor = session->monitor;
        if (df_gvs_monitor_receive(&session->monitor, frame, source_ipv4,
                now_ms, &result) != DF_OK)
            return DF_ERR_INVALID;
        if (result.keepalive_reply && manager->callbacks.emit_control(
                session->station, session->station_ipv4,
                manager->config.local, 0x03U, 0x52U, NULL, 0U,
                manager->callbacks.context) != DF_OK) {
            session->monitor = previous_monitor;
            return DF_ERR_IO;
        }
        if (result.retrying && manager->callbacks.emit_control(
                session->station, session->station_ipv4,
                manager->config.local, 0x03U, 0x82U, NULL, 0U,
                manager->callbacks.context) != DF_OK) {
            session->monitor = previous_monitor;
            return DF_ERR_IO;
        }
        if (result.confirmed) session->state = DF_MEDIA_SESSION_AWAITING_VIDEO;
        if (result.retrying) {
            if (df_media_session_reset_pipeline(session) != DF_OK) {
                session->monitor = previous_monitor;
                return DF_ERR_IO;
            }
            session->state = DF_MEDIA_SESSION_REQUESTING;
        }
        if (result.failed) {
            session->state = DF_MEDIA_SESSION_FAILED;
            if (df_media_session_manager_release_failed(manager, session) != DF_OK)
                return DF_ERR_IO;
        }
        if (result.stopped) {
            if (df_media_session_manager_stop_resources(session) != DF_OK)
                return DF_ERR_IO;
            df_media_session_reset(session);
            if (manager->active_count > 0U) manager->active_count--;
        }
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

static struct df_media_session *df_media_session_manager_oldest_preview(
    struct df_media_session_manager *manager) {
    struct df_media_session *victim = NULL;
    size_t index;

    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];
        if (!session->active || session->purpose != DF_MEDIA_SESSION_PREVIEW)
            continue;
        if (victim == NULL || session->started_ms < victim->started_ms)
            victim = session;
    }
    return victim;
}

static int df_media_session_manager_preempt_preview(
    struct df_media_session_manager *manager,
    struct df_media_session *session, uint64_t now_ms) {
    struct df_gvs_monitor previous_monitor;
    struct df_gvs_monitor_action action;

    if (manager == NULL || session == NULL || !session->active ||
        session->purpose != DF_MEDIA_SESSION_PREVIEW)
        return DF_ERR_INVALID;
    previous_monitor = session->monitor;
    if (session->monitor.state != DF_GVS_MONITOR_IDLE) {
        if (df_gvs_monitor_stop(&session->monitor, session->generation,
                now_ms) != DF_OK ||
            df_gvs_monitor_step(&session->monitor, now_ms, &action) != DF_OK) {
            session->monitor = previous_monitor;
            return DF_ERR_INVALID;
        }
        if (action.send && manager->callbacks.emit_control(
                action.destination, session->station_ipv4, action.source,
                action.family, action.opcode, action.payload,
                action.payload_length, manager->callbacks.context) != DF_OK) {
            session->monitor = previous_monitor;
            return DF_ERR_IO;
        }
    }
    return df_media_session_manager_stop_resources(session);
}

static int df_media_session_manager_prepare_call(
    struct df_media_session_manager *manager,
    const struct df_media_station_config_v3 *station, uint64_t generation,
    uint64_t call_generation, uint64_t now_ms,
    struct df_media_session *session) {
    uint32_t station_ipv4;
    uint64_t available_kib;
    int result;

    if (session == NULL) return DF_ERR_INVALID;
    df_media_session_reset(session);
    station_ipv4 = station->ipv4;
    if (station_ipv4 == 0U &&
        (manager->callbacks.resolve_route(station->logical_address, now_ms,
            &station_ipv4, manager->callbacks.context) != DF_OK ||
         station_ipv4 == 0U)) return DF_MEDIA_ERROR_ROUTE_UNAVAILABLE;
    if (manager->config.min_free_kib != 0U &&
        (manager->callbacks.available_memory(&available_kib,
            manager->callbacks.context) != DF_OK ||
         available_kib < manager->config.min_free_kib))
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;
    result = df_media_session_prepare(session, station, station_ipv4,
        DF_MEDIA_SESSION_CALL, now_ms);
    if (result != DF_OK) return DF_ERR_INVALID;
    session->stop_resources = manager->resource_hooks.stop;
    session->resource_context = manager->resource_hooks.context;
    if (manager->resource_hooks.start(session, generation,
            manager->resource_hooks.context) != DF_OK ||
        df_media_session_activate(session, generation) != DF_OK ||
        df_media_session_manager_bind_call_monitor(manager, session,
            now_ms) != DF_OK) {
        (void)df_media_session_manager_stop_resources(session);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    session->call_generation = call_generation;
    return DF_OK;
}

int df_media_session_manager_incoming_call(
    struct df_media_session_manager *manager, const char *station_id,
    uint64_t call_generation, uint64_t now_ms) {
    const struct df_media_station_config_v3 *station;
    struct df_media_session *session;
    struct df_media_session *victim;
    struct df_media_session candidate = {0};
    uint64_t generation;
    int result;

    if (manager == NULL || !manager->initialized || station_id == NULL ||
        call_generation == 0U) return DF_ERR_INVALID;
    manager->preempted_station_id[0] = '\0';
    manager->preempted_generation = 0U;
    station = df_media_session_manager_find_station(manager, station_id);
    if (station == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!manager->config.enabled || !station->enabled)
        return DF_MEDIA_ERROR_STATION_DISABLED;
    session = df_media_session_manager_find_active(manager, station_id);
    if (session != NULL && session->purpose == DF_MEDIA_SESSION_CALL) {
        if (call_generation == session->call_generation)
            return DF_OK;
        if (call_generation < session->call_generation)
            return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    }
    if (manager->next_generation == UINT64_MAX)
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;
    generation = manager->next_generation + 1U;
    if (session != NULL) {
        if (df_media_session_manager_call_monitor_preflight(manager, session,
                now_ms) != DF_OK)
            return DF_MEDIA_ERROR_ENCODER_FAILED;
        result = df_media_session_upgrade_to_call(session, generation,
            call_generation, now_ms);
        if (result != DF_OK) return result;
        if (df_media_session_manager_bind_call_monitor(manager, session,
                now_ms) != DF_OK)
            return DF_MEDIA_ERROR_ENCODER_FAILED;
        manager->next_generation = generation;
        df_media_session_manager_advance_revisions(manager, session);
        return DF_OK;
    }
    if (manager->active_count >= manager->capacity) {
        if (manager->config.incoming_call_policy == DF_MEDIA_CALL_PRESERVE_PREVIEWS)
            return DF_MEDIA_ERROR_CAPACITY_BUSY;
        victim = df_media_session_manager_oldest_preview(manager);
        if (victim == NULL) return DF_MEDIA_ERROR_CAPACITY_BUSY;
        result = df_media_session_manager_prepare_call(manager, station,
            generation, call_generation, now_ms, &candidate);
        if (result != DF_OK) return result;
        if (df_media_session_manager_preempt_preview(
                manager, victim, now_ms) != DF_OK) {
            (void)df_media_session_manager_stop_resources(&candidate);
            df_media_session_reset(&candidate);
            return DF_MEDIA_ERROR_ENCODER_FAILED;
        }
        (void)snprintf(manager->preempted_station_id,
            sizeof(manager->preempted_station_id), "%s", victim->station_id);
        manager->preempted_generation = victim->generation;
        df_media_session_reset(victim);
        *victim = candidate;
        manager->next_generation = generation;
        df_media_session_manager_advance_revisions(manager, victim);
        return DF_OK;
    }
    session = df_media_session_manager_find_free(manager);
    if (session == NULL) return DF_MEDIA_ERROR_CAPACITY_BUSY;
    result = df_media_session_manager_prepare_call(manager, station,
        generation, call_generation, now_ms, session);
    if (result != DF_OK) return result;
    manager->active_count++;
    manager->next_generation = generation;
    df_media_session_manager_advance_revisions(manager, session);
    return DF_OK;
}

int df_media_session_manager_command(struct df_media_session_manager *manager,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    const struct df_media_station_config_v3 *station;
    struct df_media_session *session;
    int result;

    if (manager == NULL || !manager->initialized || key == NULL ||
        key->station_id == NULL) return DF_ERR_INVALID;
    if (command != DF_MEDIA_MODULE_COMMAND_STOP &&
        command != DF_MEDIA_MODULE_COMMAND_VIEWER) return DF_ERR_INVALID;
    station = df_media_session_manager_find_station(manager, key->station_id);
    if (station == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!manager->config.enabled || !station->enabled)
        return DF_MEDIA_ERROR_STATION_DISABLED;
    session = df_media_session_manager_find_active(manager, key->station_id);
    if (session == NULL || session->generation != key->generation ||
        key->generation == 0U) return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    if (command == DF_MEDIA_MODULE_COMMAND_STOP) {
        if (session->monitor.state == DF_GVS_MONITOR_REQUESTING ||
            session->monitor.state == DF_GVS_MONITOR_AWAITING_VIDEO ||
            session->monitor.state == DF_GVS_MONITOR_PUBLISHING ||
            session->monitor.state == DF_GVS_MONITOR_VIEWING) {
            struct df_gvs_monitor_action action;
            struct df_gvs_monitor previous_monitor = session->monitor;

            if (df_gvs_monitor_stop(&session->monitor, key->generation,
                    now_ms) != DF_OK ||
                df_gvs_monitor_step(&session->monitor, now_ms, &action) != DF_OK)
                return DF_ERR_INVALID;
            if (action.send && manager->callbacks.emit_control(
                    action.destination, session->station_ipv4, action.source,
                    action.family, action.opcode, action.payload,
                    action.payload_length, manager->callbacks.context) != DF_OK) {
                session->monitor = previous_monitor;
                return DF_ERR_IO;
            }
            session->state = DF_MEDIA_SESSION_STOPPING;
            session->viewer_active = false;
            if (df_media_session_stop_pipeline(session) != DF_OK)
                return DF_MEDIA_ERROR_ENCODER_FAILED;
            df_media_session_manager_advance_revisions(manager, session);
            return DF_OK;
        }
        if (df_media_session_manager_stop_resources(session) != DF_OK)
            return DF_MEDIA_ERROR_ENCODER_FAILED;
        result = df_media_session_command(session, command, key, active,
            now_ms);
        if (result != DF_OK) return result;
        df_media_session_reset(session);
        manager->active_count--;
        df_media_session_manager_advance_revisions(manager, session);
        return DF_OK;
    }
    result = df_media_session_command(session, command, key, active, now_ms);
    if (result == DF_OK)
        df_media_session_manager_advance_revisions(manager, session);
    return result;
}

size_t df_media_session_manager_active(
    const struct df_media_session_manager *manager) {
    return manager == NULL || !manager->initialized ? 0U :
        manager->active_count;
}

size_t df_media_session_manager_capacity(
    const struct df_media_session_manager *manager) {
    return manager == NULL || !manager->initialized ? 0U : manager->capacity;
}

const struct df_media_session *df_media_session_manager_lookup(
    const struct df_media_session_manager *manager,
    const struct df_media_session_key *key) {
    size_t index;

    if (manager == NULL || !manager->initialized || key == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (df_media_session_matches_key(&manager->sessions[index], key))
            return &manager->sessions[index];
    }
    return NULL;
}

int df_media_session_manager_push_jpeg(struct df_media_session_manager *manager,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    const uint8_t *jpeg, size_t length, uint16_t width, uint16_t height,
    uint64_t timestamp_ms) {
    size_t index;

    if (manager == NULL || !manager->initialized || source == NULL ||
        destination == NULL || jpeg == NULL || source_ipv4 == 0U ||
        memcmp(destination, manager->config.local,
            sizeof(manager->config.local)) != 0) {
        df_media_video_reject_log(
            df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_DESTINATION),
            NULL, source_ipv4);
        return DF_ERR_INVALID;
    }
    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];

        if (!session->active ||
            memcmp(source, session->station, sizeof(session->station)) != 0)
            continue;
        if (source_ipv4 != session->station_ipv4) {
            df_media_video_reject_log(
                df_media_video_reject_reason_name(
                    DF_MEDIA_VIDEO_REJECT_SOURCE_IPV4), NULL, source_ipv4);
            return DF_ERR_INVALID;
        }
        {
            struct df_gvs_monitor_result monitor_result;
            int result;

            if (session->monitor.state != DF_GVS_MONITOR_IDLE &&
                df_gvs_monitor_admit_jpeg(&session->monitor, source,
                    destination, source_ipv4, session->generation,
                    timestamp_ms, &monitor_result) != DF_OK) {
                df_media_monitor_reject_log(session, source, destination,
                    source_ipv4, timestamp_ms, &monitor_result);
                df_media_video_reject_log(
                    df_media_video_reject_reason_name(
                        DF_MEDIA_VIDEO_REJECT_MONITOR), NULL, source_ipv4);
                return DF_ERR_INVALID;
            }
            result = df_media_session_push_jpeg(session, &manager->config,
                &manager->credentials, jpeg, length, width, height,
                timestamp_ms);

            if (session->state == DF_MEDIA_SESSION_FAILED)
                (void)df_media_session_manager_release_failed(manager, session);
            else if (result == DF_OK &&
                session->monitor.state == DF_GVS_MONITOR_AWAITING_VIDEO &&
                df_gvs_monitor_mark_publishing(&session->monitor,
                    session->generation, timestamp_ms) != DF_OK)
                return DF_ERR_IO;
            if (result != DF_OK) {
                df_media_video_reject_log(
                    df_media_video_reject_reason_name(
                        DF_MEDIA_VIDEO_REJECT_ENCODER), NULL, source_ipv4);
            }
            return result;
        }
    }
    return DF_ERR_INVALID;
}

int df_media_session_manager_push_video(
    struct df_media_session_manager *manager,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4,
    uint64_t timestamp_ms) {
    size_t index;

    if (manager == NULL || !manager->initialized || packet == NULL ||
        source_ipv4 == 0U ||
        memcmp(packet->destination, manager->config.local,
            sizeof(manager->config.local)) != 0) {
        df_media_video_reject_log("destination_mismatch", packet, source_ipv4);
        return DF_ERR_INVALID;
    }
    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];
        const uint8_t *jpeg = NULL;
        size_t length = 0U;
        uint16_t width = 0U;
        uint16_t height = 0U;
        int result;

        if (!session->active ||
            memcmp(packet->source, session->station,
                sizeof(session->station)) != 0)
            continue;
        if (source_ipv4 != session->station_ipv4) {
            df_media_video_reject_log("source_ipv4_mismatch", packet,
                source_ipv4);
            return DF_ERR_INVALID;
        }
        if (session->monitor.state == DF_GVS_MONITOR_REQUESTING &&
            session->monitor.retry_waiting)
            return DF_OK;
        result = df_gvs_video_reassembly_push(
            &session->video, packet, &jpeg, &length);
        if (result == DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE ||
            result == DF_GVS_VIDEO_REASSEMBLY_DUPLICATE ||
            result == DF_GVS_VIDEO_REASSEMBLY_LATE)
            return DF_OK;
        if (result != DF_GVS_VIDEO_REASSEMBLY_COMPLETE) {
            df_media_video_reject_log("reassembly_failed", packet, source_ipv4);
            return DF_ERR_INVALID;
        }
        if (df_gvs_jpeg_validate(jpeg, length) != 0) {
            df_media_video_reject_log("jpeg_invalid", packet, source_ipv4);
            return DF_ERR_INVALID;
        }
        if (df_gvs_jpeg_dimensions(jpeg, length, &width, &height) != 0) {
            df_media_video_reject_log("jpeg_dimensions_invalid", packet,
                source_ipv4);
            return DF_ERR_INVALID;
        }
        return df_media_session_manager_push_jpeg(manager, packet->source,
            packet->destination, source_ipv4, jpeg, length, width, height,
            timestamp_ms);
    }
    df_media_video_reject_log("session_not_found", packet, source_ipv4);
    return DF_ERR_INVALID;
}

int df_media_session_manager_tick(struct df_media_session_manager *manager,
    uint64_t now_ms) {
    size_t index;
    int overall = DF_OK;

    if (manager == NULL || !manager->initialized) return DF_ERR_INVALID;
    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];
        struct df_gvs_monitor previous_monitor;

        if (!session->active)
            continue;
        if (session->state == DF_MEDIA_SESSION_FAILED) {
            if (df_media_session_manager_release_failed(manager, session) != DF_OK)
                overall = DF_ERR_IO;
            continue;
        }
        {
            struct df_gvs_monitor_action action;
            previous_monitor = session->monitor;
            if (df_gvs_monitor_step(&session->monitor, now_ms, &action) != DF_OK)
                overall = DF_ERR_IO;
            else if (action.send && manager->callbacks.emit_control(
                    action.destination, session->station_ipv4, action.source,
                    action.family, action.opcode, action.payload,
                    action.payload_length, manager->callbacks.context) != DF_OK) {
                session->monitor = previous_monitor;
                overall = DF_ERR_IO;
            }
        }
        if (session->monitor.state == DF_GVS_MONITOR_FAILED) {
            session->state = DF_MEDIA_SESSION_FAILED;
            if (df_media_session_manager_release_failed(manager, session) != DF_OK)
                overall = DF_ERR_IO;
            continue;
        }
        if (session->state == DF_MEDIA_SESSION_STOPPING &&
            session->monitor.state == DF_GVS_MONITOR_IDLE) {
            if (df_media_session_manager_stop_resources(session) != DF_OK) {
                overall = DF_ERR_IO;
            } else {
                df_media_session_reset(session);
                if (manager->active_count > 0U) manager->active_count--;
            }
            continue;
        }
        if ((session->state == DF_MEDIA_SESSION_PUBLISHING ||
             session->state == DF_MEDIA_SESSION_VIEWING) &&
            session->frames_received != 0U && now_ms >= session->last_frame_ms &&
            now_ms - session->last_frame_ms >=
                DF_MEDIA_SESSION_FRAME_STALL_TIMEOUT_MS) {
            if (df_media_session_manager_emit_stop_best_effort(manager,
                    session, now_ms) != DF_OK)
                overall = DF_ERR_IO;
            session->state = DF_MEDIA_SESSION_FAILED;
            session->last_error = DF_MEDIA_ERROR_VIDEO_STALLED;
            if (df_media_session_manager_release_failed(manager, session) !=
                    DF_OK)
                overall = DF_ERR_IO;
            continue;
        }
        if (df_media_encoder_is_running(&session->encoder) &&
            df_media_session_tick_pipeline(session, now_ms) != DF_OK)
            overall = DF_ERR_IO;
        if (session->state == DF_MEDIA_SESSION_FAILED)
            (void)df_media_session_manager_release_failed(manager, session);
    }
    return overall;
}

static void *df_media_module_api_create_v3(
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    struct df_media_session_manager *manager = calloc(1U, sizeof(*manager));

    if (manager == NULL ||
        df_media_session_manager_init(manager, config, callbacks) != DF_OK) {
        free(manager);
        return NULL;
    }
    return manager;
}

static void df_media_module_api_destroy_v3(void *instance) {
    struct df_media_session_manager *manager = instance;

    if (manager == NULL) return;
    df_media_session_manager_destroy(manager);
    free(manager);
}

static int df_media_module_api_start_v3(void *instance, const char *station_id,
    enum df_media_session_purpose purpose, uint64_t request_generation,
    uint64_t now_ms) {
    struct df_media_session_manager *manager = instance;
    uint64_t generation = 0U;

    if (purpose == DF_MEDIA_SESSION_CALL)
        return df_media_session_manager_incoming_call(manager, station_id,
            request_generation, now_ms);
    if (purpose != DF_MEDIA_SESSION_PREVIEW || request_generation != 0U)
        return DF_ERR_INVALID;
    return df_media_session_manager_start(manager, station_id, purpose, now_ms,
        &generation);
}

static int df_media_module_api_command_v3(void *instance,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    return df_media_session_manager_command(instance, command, key, active,
        now_ms);
}

static int df_media_module_api_receive_control_v3(void *instance,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    return df_media_session_manager_receive_control(instance, frame,
        source_ipv4, now_ms);
}

static int df_media_module_api_push_jpeg_v3(void *instance,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    const uint8_t *jpeg, size_t length, uint16_t width, uint16_t height,
    uint64_t timestamp_ms) {
    return df_media_session_manager_push_jpeg(instance, source, destination,
        source_ipv4, jpeg, length, width, height, timestamp_ms);
}

static int df_media_module_api_push_video_v3(void *instance,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4,
    uint64_t timestamp_ms) {
    return df_media_session_manager_push_video(instance, packet, source_ipv4,
        timestamp_ms);
}

static int df_media_module_api_tick_v3(void *instance, uint64_t now_ms) {
    return df_media_session_manager_tick(instance, now_ms);
}

static bool df_media_session_status_ready(
    const struct df_media_session *session) {
    return session != NULL &&
        (session->state == DF_MEDIA_SESSION_PUBLISHING ||
         session->state == DF_MEDIA_SESSION_VIEWING) &&
        session->frames_received != 0U &&
        df_media_encoder_is_running(&session->encoder);
}

static uint64_t df_media_status_mix(uint64_t hash, uint64_t value) {
    unsigned index;

    for (index = 0U; index < 8U; index++) {
        hash ^= (uint8_t)(value >> (index * 8U));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t df_media_string_fingerprint(uint64_t fingerprint,
    const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    while (cursor != NULL && *cursor != '\0') {
        fingerprint ^= *cursor++;
        fingerprint *= UINT64_C(1099511628211);
    }
    return fingerprint;
}

static uint64_t df_media_session_status_fingerprint(
    const struct df_media_session *session) {
    uint64_t fingerprint = UINT64_C(1469598103934665603);

    fingerprint = df_media_string_fingerprint(fingerprint,
        session->station_id);
    fingerprint = df_media_status_mix(fingerprint, session->generation);
    fingerprint = df_media_status_mix(fingerprint, (uint64_t)session->purpose);
    fingerprint = df_media_status_mix(fingerprint, (uint64_t)session->state);
    fingerprint = df_media_status_mix(fingerprint,
        (uint64_t)session->last_error);
    fingerprint = df_media_status_mix(fingerprint, session->active ? 1U : 0U);
    fingerprint = df_media_status_mix(fingerprint,
        session->viewer_active ? 1U : 0U);
    fingerprint = df_media_status_mix(fingerprint,
        df_media_encoder_is_running(&session->encoder) ? 1U : 0U);
    fingerprint = df_media_status_mix(fingerprint,
        session->frames_received != 0U ? 1U : 0U);
    fingerprint = df_media_status_mix(fingerprint, session->started_ms);
    fingerprint = df_media_status_mix(fingerprint,
        session->queue.dropped_oldest);
    return fingerprint;
}

static void df_media_status_revision_update(uint64_t fingerprint,
    uint64_t *stored_fingerprint, uint64_t *revision, bool *initialized) {
    if (!*initialized) {
        *stored_fingerprint = fingerprint;
        if (*revision == 0U) *revision = 1U;
        *initialized = true;
    } else if (*stored_fingerprint != fingerprint) {
        *stored_fingerprint = fingerprint;
        if (*revision != UINT64_MAX) (*revision)++;
    }
}

static int df_media_module_api_status_v3(const void *instance,
    struct df_media_module_status_v3 *status) {
    struct df_media_session_manager *manager =
        (struct df_media_session_manager *)instance;
    size_t capacity;
    size_t copied = 0U;
    size_t required = 0U;
    size_t active_encoders = 0U;
    size_t index;
    uint64_t fingerprint = UINT64_C(1469598103934665603);

    if (manager == NULL || !manager->initialized || status == NULL)
        return DF_ERR_INVALID;
    capacity = status->session_count;
    for (index = 0U; index < manager->capacity; index++) {
        struct df_media_session *session = &manager->sessions[index];
        struct df_media_session_status_v3 *entry;
        uint64_t session_fingerprint;

        if (!session->active) continue;
        required++;
        session_fingerprint = df_media_session_status_fingerprint(session);
        df_media_status_revision_update(session_fingerprint,
            &session->status_fingerprint, &session->status_revision,
            &session->status_initialized);
        fingerprint = df_media_status_mix(fingerprint, session_fingerprint);
        if (df_media_encoder_is_running(&session->encoder)) active_encoders++;
        if (status->sessions == NULL || copied >= capacity) continue;
        entry = &status->sessions[copied++];
        memset(entry, 0, sizeof(*entry));
        (void)snprintf(entry->station_id, sizeof(entry->station_id), "%s",
            session->station_id);
        (void)snprintf(entry->stream_name, sizeof(entry->stream_name), "%s",
            session->stream_name);
        entry->generation = session->generation;
        entry->purpose = session->purpose;
        entry->state = session->state;
        entry->last_error = session->last_error;
        entry->active = session->active;
        entry->ready = df_media_session_status_ready(session);
        entry->viewer_active = session->viewer_active;
        entry->encoder_running =
            df_media_encoder_is_running(&session->encoder);
        entry->started_ms = session->started_ms;
        entry->status_revision = session->status_revision;
        entry->queue_drops = session->queue.dropped_oldest;
    }
    fingerprint = df_media_status_mix(fingerprint, required);
    fingerprint = df_media_status_mix(fingerprint, active_encoders);
    fingerprint = df_media_string_fingerprint(fingerprint,
        manager->preempted_station_id);
    fingerprint = df_media_status_mix(fingerprint,
        manager->preempted_generation);
    fingerprint = df_media_string_fingerprint(fingerprint,
        manager->failed_station_id);
    fingerprint = df_media_status_mix(fingerprint, manager->failed_generation);
    fingerprint = df_media_status_mix(fingerprint, manager->failed_error);
    df_media_status_revision_update(fingerprint, &manager->status_fingerprint,
        &manager->status_revision, &manager->status_initialized);
    status->required_session_count = required;
    status->session_count = copied;
    status->configured_capacity = manager->config.max_encoders == 0U ? 1U :
        manager->config.max_encoders;
    status->effective_capacity = manager->capacity;
    status->active_encoders = active_encoders;
    status->status_revision = manager->status_revision;
    (void)snprintf(status->preempted_station_id,
        sizeof(status->preempted_station_id), "%s",
        manager->preempted_station_id);
    status->preempted_generation = manager->preempted_generation;
    (void)snprintf(status->failed_station_id, sizeof(status->failed_station_id),
        "%s", manager->failed_station_id);
    status->failed_generation = manager->failed_generation;
    status->failed_error = manager->failed_error;
    return DF_OK;
}

const struct df_media_module_api_v3 df_media_module_api_v3 = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_api_v3),
    .create = df_media_module_api_create_v3,
    .destroy = df_media_module_api_destroy_v3,
    .start = df_media_module_api_start_v3,
    .command = df_media_module_api_command_v3,
    .receive_control = df_media_module_api_receive_control_v3,
    .push_jpeg = df_media_module_api_push_jpeg_v3,
    .push_video = df_media_module_api_push_video_v3,
    .tick = df_media_module_api_tick_v3,
    .status = df_media_module_api_status_v3,
};
