#include "media_session.h"

#include <stdio.h>
#include <string.h>

#include "doorfast.h"

static void df_media_session_queue_destroy(struct df_media_session *session) {
    if (session != NULL && session->queue_initialized) {
        df_media_frame_queue_destroy(&session->queue);
        session->queue_initialized = false;
    }
}

static void df_media_session_fail_encoder(struct df_media_session *session) {
    if (session == NULL) return;
    (void)df_media_encoder_stop(&session->encoder, 100U);
    df_media_session_queue_destroy(session);
    session->state = DF_MEDIA_SESSION_FAILED;
    session->last_error = DF_MEDIA_ERROR_ENCODER_FAILED;
}

static int df_media_session_start_encoder(struct df_media_session *session,
    const struct df_media_module_config_v3 *module_config,
    const struct df_media_credentials *credentials, uint16_t width,
    uint16_t height) {
    const struct df_media_encoder_config config = {
        .program = "ffmpeg",
        .host = module_config->go2rtc_host,
        .port = module_config->go2rtc_port,
        .stream = session->stream_name,
        .username = module_config->rtsp_username,
        .fps = module_config->fps,
        .bitrate_kbps = module_config->bitrate_kbps,
        .encoder = module_config->encoder,
        .resolution = module_config->resolution,
        .profile = module_config->profile,
        .width = width,
        .height = height,
    };

    return df_media_encoder_start(&session->encoder, &config, credentials,
        session->media_generation);
}

static int df_media_session_flush_pending(struct df_media_session *session) {
    struct df_media_frame frame;

    if (session == NULL || session->encoder.pending_frame == NULL)
        return DF_OK;
    frame.data = session->encoder.pending_frame;
    frame.length = session->encoder.pending_length;
    frame.generation = session->encoder.pending_generation;
    frame.timestamp_ms = session->encoder.pending_timestamp_ms;
    return df_media_encoder_write_frame(&session->encoder, &frame);
}

static enum df_media_session_state_v3 df_media_session_state_after_viewer(
    const struct df_media_session *session) {
    if (session->monitor.state == DF_GVS_MONITOR_AWAITING_VIDEO)
        return DF_MEDIA_SESSION_AWAITING_VIDEO;
    if (session->monitor.state == DF_GVS_MONITOR_REQUESTING)
        return DF_MEDIA_SESSION_REQUESTING;
    if (session->monitor.state == DF_GVS_MONITOR_PUBLISHING ||
        session->monitor.state == DF_GVS_MONITOR_VIEWING ||
        (session->frames_received != 0U &&
         df_media_encoder_is_running(&session->encoder)))
        return DF_MEDIA_SESSION_PUBLISHING;
    return DF_MEDIA_SESSION_REQUESTING;
}

void df_media_session_reset(struct df_media_session *session) {
    if (session != NULL) {
        uint64_t status_fingerprint = session->status_fingerprint;
        uint64_t status_revision = session->status_revision;
        bool status_initialized = session->status_initialized;

        df_gvs_video_reassembly_reset(&session->video);
        memset(session, 0, sizeof(*session));
        session->status_fingerprint = status_fingerprint;
        session->status_revision = status_revision;
        session->status_initialized = status_initialized;
    }
}
int df_media_session_prepare(struct df_media_session *session,
    const struct df_media_station_config_v3 *station, uint32_t station_ipv4,
    enum df_media_session_purpose purpose, uint64_t now_ms) {
    if (session == NULL || station == NULL || station->id == NULL ||
        station->stream_name == NULL || station_ipv4 == 0U ||
        strlen(station->id) >= sizeof(session->station_id) ||
        strlen(station->stream_name) >= sizeof(session->stream_name) ||
        (purpose != DF_MEDIA_SESSION_PREVIEW &&
         purpose != DF_MEDIA_SESSION_CALL)) return DF_ERR_INVALID;

    df_media_session_reset(session);
    (void)snprintf(session->station_id, sizeof(session->station_id), "%s",
        station->id);
    (void)snprintf(session->stream_name, sizeof(session->stream_name), "%s",
        station->stream_name);
    memcpy(session->station, station->logical_address,
        sizeof(session->station));
    session->station_ipv4 = station_ipv4;
    session->purpose = purpose;
    session->state = DF_MEDIA_SESSION_IDLE;
    session->last_error = DF_MEDIA_ERROR_NONE;
    df_gvs_video_reassembly_init(&session->video);
    df_gvs_monitor_init(&session->monitor);
    session->reserved = true;
    session->started_ms = now_ms;
    return DF_OK;
}

int df_media_session_activate(struct df_media_session *session,
    uint64_t generation) {
    if (session == NULL || !session->reserved || session->active ||
        generation == 0U) return DF_ERR_INVALID;
    session->generation = generation;
    session->media_generation = generation;
    session->state = DF_MEDIA_SESSION_REQUESTING;
    session->reserved = false;
    session->active = true;
    return DF_OK;
}

int df_media_session_publish(struct df_media_session *session,
    const struct df_media_station_config_v3 *station, uint32_t station_ipv4,
    enum df_media_session_purpose purpose, uint64_t generation,
    uint64_t now_ms) {
    int result = df_media_session_prepare(session, station, station_ipv4,
        purpose, now_ms);

    if (result != DF_OK) return result;
    result = df_media_session_activate(session, generation);
    if (result != DF_OK) df_media_session_reset(session);
    return result;
}

bool df_media_session_matches_station(const struct df_media_session *session,
    const char *station_id) {
    return session != NULL && station_id != NULL && session->active &&
        strcmp(session->station_id, station_id) == 0;
}

bool df_media_session_matches_key(const struct df_media_session *session,
    const struct df_media_session_key *key) {
    return key != NULL && key->generation != 0U &&
        df_media_session_matches_station(session, key->station_id) &&
        session->generation == key->generation;
}

int df_media_session_command(struct df_media_session *session,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    (void)now_ms;
    if (session == NULL || key == NULL || key->station_id == NULL)
        return DF_ERR_INVALID;
    if (!df_media_session_matches_station(session, key->station_id))
        return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!df_media_session_matches_key(session, key))
        return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    if (command == DF_MEDIA_MODULE_COMMAND_STOP) {
        session->state = DF_MEDIA_SESSION_STOPPING;
        session->active = false;
        session->viewer_active = false;
        return DF_OK;
    }
    if (command == DF_MEDIA_MODULE_COMMAND_VIEWER) {
        session->viewer_active = active;
        if (active) session->state = DF_MEDIA_SESSION_VIEWING;
        else if (session->state == DF_MEDIA_SESSION_VIEWING)
            session->state = df_media_session_state_after_viewer(session);
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

int df_media_session_stop_pipeline(struct df_media_session *session) {
    int result;

    if (session == NULL) return DF_ERR_INVALID;
    result = df_media_encoder_stop(&session->encoder, 100U);
    df_media_session_queue_destroy(session);
    return result;
}

int df_media_session_reset_pipeline(struct df_media_session *session) {
    int result;

    if (session == NULL) return DF_ERR_INVALID;
    result = df_media_session_stop_pipeline(session);
    df_gvs_video_reassembly_reset(&session->video);
    df_gvs_video_reassembly_init(&session->video);
    session->frames_received = 0U;
    session->last_frame_ms = 0U;
    session->viewer_active = false;
    session->last_error = DF_MEDIA_ERROR_NONE;
    return result;
}

int df_media_session_upgrade_to_call(struct df_media_session *session,
    uint64_t generation, uint64_t call_generation, uint64_t now_ms) {
    if (session == NULL || !session->active || generation == 0U ||
        call_generation == 0U || generation == session->generation ||
        now_ms < session->monitor.last_now_ms) return DF_ERR_INVALID;
    if (df_media_encoder_is_running(&session->encoder) &&
        df_media_encoder_stop(&session->encoder, 100U) != DF_OK)
        return DF_ERR_IO;
    if (session->queue_initialized &&
        df_media_frame_queue_reset(&session->queue, generation) != DF_OK)
        return DF_ERR_IO;
    df_gvs_video_reassembly_reset(&session->video);
    df_gvs_video_reassembly_init(&session->video);
    session->purpose = DF_MEDIA_SESSION_CALL;
    session->generation = generation;
    session->media_generation = generation;
    session->call_generation = call_generation;
    session->started_ms = now_ms;
    session->frames_received = 0U;
    session->last_frame_ms = 0U;
    session->state = DF_MEDIA_SESSION_REQUESTING;
    session->last_error = DF_MEDIA_ERROR_NONE;
    session->viewer_active = false;
    return DF_OK;
}

int df_media_session_tick_pipeline(struct df_media_session *session,
    uint64_t now_ms) {
    int result;

    if (session == NULL || !session->active) return DF_ERR_INVALID;
    if (session->state == DF_MEDIA_SESSION_FAILED) return DF_OK;
    result = df_media_encoder_tick(&session->encoder, now_ms);
    if (result != DF_OK || session->encoder.encoder_exited) {
        df_media_session_fail_encoder(session);
        return result == DF_OK ? DF_OK : DF_ERR_IO;
    }
    return DF_OK;
}

int df_media_session_push_jpeg(struct df_media_session *session,
    const struct df_media_module_config_v3 *module_config,
    const struct df_media_credentials *credentials, const uint8_t *jpeg,
    size_t length, uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    struct df_media_frame frame;
    int result;

    if (session == NULL || module_config == NULL || jpeg == NULL ||
        !session->active || session->state == DF_MEDIA_SESSION_FAILED ||
        df_gvs_jpeg_validate(jpeg, length) != 0 || width == 0U ||
        height == 0U) return DF_ERR_INVALID;
    result = df_media_session_tick_pipeline(session, timestamp_ms);
    if (result != DF_OK || session->state == DF_MEDIA_SESSION_FAILED)
        return DF_ERR_IO;
    if (df_media_encoder_requires_restart(&session->encoder, width, height)) {
        if (df_media_encoder_stop(&session->encoder, 100U) != DF_OK) {
            df_media_session_fail_encoder(session);
            return DF_ERR_IO;
        }
        if (session->queue_initialized &&
            df_media_frame_queue_reset(&session->queue,
                session->media_generation) != DF_OK) {
            df_media_session_fail_encoder(session);
            return DF_ERR_IO;
        }
    }
    if (!session->queue_initialized) {
        if (df_media_frame_queue_init(&session->queue,
                session->media_generation,
                DF_GVS_VIDEO_MAX_FRAME) != DF_OK) return DF_ERR_IO;
        session->queue_initialized = true;
    }
    if (df_media_frame_queue_push(&session->queue, jpeg, length,
            session->media_generation, timestamp_ms) != DF_OK) return DF_ERR_IO;
    session->frames_received++;
    session->last_frame_ms = timestamp_ms;
    if (!df_media_encoder_is_running(&session->encoder) &&
        df_media_session_start_encoder(session, module_config, credentials,
            width, height) != DF_OK) {
        df_media_session_fail_encoder(session);
        return DF_ERR_IO;
    }
    result = df_media_session_flush_pending(session);
    if (result != DF_OK && result != DF_MEDIA_ENCODER_RETRY) {
        if (session->encoder.encoder_exited)
            df_media_session_fail_encoder(session);
        return DF_ERR_IO;
    }
    if (result == DF_MEDIA_ENCODER_RETRY) {
        session->state = DF_MEDIA_SESSION_PUBLISHING;
        session->last_error = DF_MEDIA_ERROR_NONE;
        return DF_OK;
    }
    if (df_media_frame_queue_pop(&session->queue, &frame) == DF_OK) {
        result = df_media_encoder_write_frame(&session->encoder, &frame);
        if (result == DF_MEDIA_ENCODER_RETRY) {
            (void)df_media_frame_queue_push(&session->queue, frame.data,
                frame.length, frame.generation, frame.timestamp_ms);
        } else if (result != DF_OK) {
            if (session->encoder.encoder_exited)
                df_media_session_fail_encoder(session);
            return result;
        }
    }
    if (session->encoder.encoder_exited) {
        df_media_session_fail_encoder(session);
        return DF_ERR_IO;
    }
    session->state = DF_MEDIA_SESSION_PUBLISHING;
    session->last_error = DF_MEDIA_ERROR_NONE;
    return DF_OK;
}
