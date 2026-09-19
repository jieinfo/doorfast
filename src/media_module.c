#include "media_module.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "doorfast.h"
#include "gvs_station.h"
#include "gvs_video_reassembly.h"
#include "media_capacity.h"

static const char *df_media_module_state_name(enum df_gvs_monitor_state state) {
    switch (state) {
    case DF_GVS_MONITOR_IDLE: return "idle";
    case DF_GVS_MONITOR_REQUESTING: return "requesting";
    case DF_GVS_MONITOR_AWAITING_VIDEO: return "awaiting_video";
    case DF_GVS_MONITOR_PUBLISHING: return "publishing";
    case DF_GVS_MONITOR_VIEWING: return "viewing";
    case DF_GVS_MONITOR_STOPPING: return "stopping";
    case DF_GVS_MONITOR_FAILED: return "failed";
    default: return "failed";
    }
}

static bool df_media_module_active(enum df_gvs_monitor_state state) {
    return state == DF_GVS_MONITOR_REQUESTING ||
        state == DF_GVS_MONITOR_AWAITING_VIDEO ||
        state == DF_GVS_MONITOR_PUBLISHING ||
        state == DF_GVS_MONITOR_VIEWING ||
        state == DF_GVS_MONITOR_STOPPING;
}

static void df_media_module_set_failure(struct df_media_module *module,
    const char *failure) {
    if (module == NULL) return;
    (void)snprintf(module->failure, sizeof(module->failure), "%s",
        failure == NULL ? "" : failure);
}

static void df_media_module_sync_status_revision(struct df_media_module *module) {
    bool encoder_running;

    if (module == NULL) return;
    encoder_running = df_media_encoder_is_running(&module->encoder);
    if (module->revision_monitor_state == module->monitor.state &&
        module->revision_generation == module->monitor.generation &&
        module->revision_queue_drops == module->queue.dropped_oldest &&
        module->revision_encoder_running == encoder_running &&
        strcmp(module->revision_failure, module->failure) == 0) return;
    module->status_revision++;
    module->revision_monitor_state = module->monitor.state;
    module->revision_generation = module->monitor.generation;
    module->revision_queue_drops = module->queue.dropped_oldest;
    module->revision_encoder_running = encoder_running;
    (void)snprintf(module->revision_failure,
        sizeof(module->revision_failure), "%s", module->failure);
}

static int df_media_module_emit_action(struct df_media_module *module,
    const struct df_gvs_monitor_action *action) {
    if (module == NULL || action == NULL || !action->send) return DF_OK;
    if (module->callbacks.emit_control == NULL ||
        module->monitor.station_ipv4 == 0U) return DF_ERR_IO;
    return module->callbacks.emit_control(action->destination,
        module->monitor.station_ipv4, action->source, action->family,
        action->opcode, action->payload,
        action->payload_length, module->callbacks.context);
}

static void df_media_module_queue_destroy(struct df_media_module *module) {
    if (module != NULL && module->queue_initialized) {
        df_media_frame_queue_destroy(&module->queue);
        module->queue_initialized = false;
    }
}

static int df_media_module_encoder_stop(struct df_media_module *module) {
    if (module == NULL || module->stop_encoder == NULL) return DF_ERR_INVALID;
    if (df_media_encoder_is_running(&module->encoder))
        return module->stop_encoder(&module->encoder, 100U);
    return module->stop_encoder(&module->encoder, 0U);
}

static int df_media_module_cleanup_media(struct df_media_module *module) {
    int result = df_media_module_encoder_stop(module);

    df_media_module_queue_destroy(module);
    return result;
}

static int df_media_module_handle_encoder_exit(struct df_media_module *module,
    uint64_t generation, uint64_t now_ms) {
    if (module == NULL || generation == 0U) return DF_ERR_INVALID;
    if (!module->encoder.encoder_exited) return DF_OK;
    if (module->monitor.state == DF_GVS_MONITOR_FAILED &&
        strcmp(module->failure, "encoder_exited") == 0) {
        df_media_module_queue_destroy(module);
        return DF_OK;
    }
    module->monitor.state = DF_GVS_MONITOR_FAILED;
    module->monitor.failure = DF_GVS_MONITOR_FAILURE_NONE;
    module->monitor.media_ready = false;
    df_media_module_set_failure(module, "encoder_exited");
    df_media_module_queue_destroy(module);
    (void)now_ms;
    df_media_module_sync_status_revision(module);
    return DF_OK;
}

static int df_media_module_report_cleanup_failure(struct df_media_module *module,
    uint64_t generation, uint64_t now_ms) {
    if (module == NULL || generation == 0U) return DF_ERR_INVALID;
    module->monitor.state = DF_GVS_MONITOR_FAILED;
    module->monitor.failure = DF_GVS_MONITOR_FAILURE_NONE;
    module->monitor.media_ready = false;
    df_media_module_set_failure(module, "encoder_exited");
    (void)now_ms;
    df_media_module_sync_status_revision(module);
    return DF_ERR_IO;
}

static int df_media_module_start_encoder(struct df_media_module *module,
    uint16_t width, uint16_t height) {
    struct df_media_encoder_config config = {
        .program = "ffmpeg",
    };

    if (module == NULL || module->config.go2rtc_host == NULL ||
        module->config.stream_name == NULL || module->config.fps == 0U ||
        module->config.bitrate_kbps == 0U) return DF_ERR_INVALID;
    config.host = module->config.go2rtc_host;
    config.port = module->config.go2rtc_port;
    config.stream = module->config.stream_name;
    config.username = module->config.rtsp_username;
    config.fps = module->config.fps;
    config.bitrate_kbps = module->config.bitrate_kbps;
    config.encoder = module->config.encoder;
    config.resolution = module->config.resolution;
    config.profile = module->config.profile;
    config.width = width;
    config.height = height;
    return module->start_encoder(&module->encoder, &config,
        &module->credentials, module->monitor.generation);
}

static int df_media_module_read_available_memory(uint64_t *available_kib,
    void *context) {
    FILE *file;
    char key[64];
    char unit[16];
    unsigned long long value;
    (void)context;

    if (available_kib == NULL) return DF_ERR_INVALID;
    file = fopen("/proc/meminfo", "r");
    if (file == NULL) return DF_ERR_IO;
    while (fscanf(file, "%63s %llu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemAvailable:") == 0) {
            (void)fclose(file);
            *available_kib = (uint64_t)value;
            return DF_OK;
        }
    }
    (void)fclose(file);
    return DF_ERR_IO;
}

static int df_media_module_copy(char *destination, size_t capacity,
    const char *source) {
    size_t length;

    if (destination == NULL || capacity == 0U) return DF_ERR_INVALID;
    if (source == NULL) source = "";
    length = strlen(source);
    if (length >= capacity) return DF_ERR_INVALID;
    memcpy(destination, source, length + 1U);
    return DF_OK;
}

static bool df_media_module_intel_render_node(void) {
    FILE *file;
    char vendor[16];

    if (access("/dev/dri/renderD128", R_OK | W_OK) != 0) return false;
    file = fopen("/sys/class/drm/renderD128/device/vendor", "r");
    if (file == NULL) return false;
    if (fgets(vendor, sizeof(vendor), file) == NULL) vendor[0] = '\0';
    (void)fclose(file);
    return strncmp(vendor, "0x8086", 6U) == 0;
}

static int df_media_module_select_encoder(struct df_media_module *module) {
    const bool render_node = access("/dev/dri/renderD128", R_OK | W_OK) == 0;
    const struct df_media_encoder_probe probe = {
        .software_available = true,
        .vaapi_available = render_node,
        .qsv_available = render_node && df_media_module_intel_render_node(),
    };
    enum df_media_encoder selected;

    if (df_media_encoder_select(module->config.encoder, &probe, &selected) != DF_OK)
        return DF_ERR_INVALID;
    module->config.encoder = selected;
    return DF_OK;
}

int df_media_module_init(struct df_media_module *module,
    const struct df_media_module_config_v2 *config,
    const struct df_media_module_callbacks_v2 *callbacks, uint64_t now_ms) {
    (void)now_ms;
    if (module == NULL || config == NULL || callbacks == NULL ||
        !config->enabled || df_gvs_station_validate(config->station) != DF_OK ||
        config->local[0] == 0U || callbacks->emit_control == NULL) {
        return DF_ERR_INVALID;
    }
    memset(module, 0, sizeof(*module));
    module->config = *config;
    module->callbacks = *callbacks;
    if (df_media_module_copy(module->go2rtc_host,
            sizeof(module->go2rtc_host), config->go2rtc_host) != DF_OK ||
        df_media_module_copy(module->stream_name,
            sizeof(module->stream_name), config->stream_name) != DF_OK ||
        df_media_module_copy(module->rtsp_username,
            sizeof(module->rtsp_username), config->rtsp_username) != DF_OK ||
        df_media_module_copy(module->credentials_path,
            sizeof(module->credentials_path), config->credentials_path == NULL ?
                DF_MEDIA_CREDENTIALS_PATH : config->credentials_path) != DF_OK)
        return DF_ERR_INVALID;
    module->config.go2rtc_host = module->go2rtc_host;
    module->config.stream_name = module->stream_name;
    module->config.rtsp_username = module->rtsp_username;
    module->config.credentials_path = module->credentials_path;
    module->config.deprecated_relay_url = NULL;
    module->callbacks.deprecated_relay_send = NULL;
    if (df_media_credentials_load(module->credentials_path,
            &module->credentials) != DF_OK ||
        df_media_module_select_encoder(module) != DF_OK) {
        (void)df_media_module_destroy(module);
        return DF_ERR_INVALID;
    }
    df_gvs_monitor_init(&module->monitor);
    if (config->first_frame_timeout_s != 0U &&
        df_gvs_monitor_set_first_frame_timeout(&module->monitor,
            (uint64_t)config->first_frame_timeout_s * 1000U) != DF_OK) {
        (void)df_media_module_destroy(module);
        return DF_ERR_INVALID;
    }
    module->start_encoder = df_media_encoder_start;
    module->stop_encoder = df_media_encoder_stop;
    module->status_revision = 1U;
    module->revision_monitor_state = module->monitor.state;
    module->revision_generation = module->monitor.generation;
    module->revision_queue_drops = module->queue.dropped_oldest;
    module->revision_encoder_running =
        df_media_encoder_is_running(&module->encoder);
    (void)snprintf(module->revision_failure,
        sizeof(module->revision_failure), "%s", module->failure);
    module->initialized = true;
    return DF_OK;
}

int df_media_module_start(struct df_media_module *module, uint64_t now_ms) {
    uint32_t station_ipv4;
    uint64_t available_kib;
    uint64_t preview_duration_ms;

    if (module == NULL || !module->initialized ||
        (module->monitor.state != DF_GVS_MONITOR_IDLE &&
         module->monitor.state != DF_GVS_MONITOR_FAILED)) return DF_ERR_INVALID;
    if (module->monitor.state == DF_GVS_MONITOR_FAILED &&
        df_media_module_cleanup_media(module) != DF_OK) return DF_ERR_IO;
    if (module->config.min_free_kib != 0U) {
        df_media_module_available_memory_fn available_memory =
            module->callbacks.available_memory == NULL ?
                df_media_module_read_available_memory :
                module->callbacks.available_memory;
        if (available_memory(&available_kib, module->callbacks.context) != DF_OK ||
            available_kib < module->config.min_free_kib) {
            df_media_module_set_failure(module, "insufficient_memory");
            df_media_module_sync_status_revision(module);
            return DF_ERR_IO;
        }
    }
    preview_duration_ms = (uint64_t)module->config.preview_timeout_s * 1000U;
    if (preview_duration_ms != 0U &&
        now_ms > UINT64_MAX - preview_duration_ms) return DF_ERR_INVALID;
    station_ipv4 = module->config.station_ipv4;
    if (station_ipv4 == 0U && module->callbacks.resolve_route != NULL &&
        module->callbacks.resolve_route(module->config.station, now_ms,
            &station_ipv4, module->callbacks.context) != DF_OK) return DF_ERR_INVALID;
    if (station_ipv4 == 0U || df_gvs_monitor_start(&module->monitor,
            module->config.local, module->config.station, station_ipv4,
            now_ms) != DF_OK) return DF_ERR_INVALID;
    df_media_module_set_failure(module, "");
    module->preview_deadline_ms = preview_duration_ms == 0U ? 0U :
        now_ms + preview_duration_ms;
    return df_media_module_tick(module, now_ms);
}

int df_media_module_command(struct df_media_module *module,
    enum df_media_module_command command, uint64_t generation, bool active,
    uint64_t now_ms) {
    if (module == NULL || !module->initialized || generation == 0U ||
        generation != module->monitor.generation) return DF_ERR_INVALID;
    if (command == DF_MEDIA_MODULE_COMMAND_STOP) {
        if (df_gvs_monitor_stop(&module->monitor, generation, now_ms) != DF_OK)
            return DF_ERR_INVALID;
        return df_media_module_tick(module, now_ms);
    }
    if (command == DF_MEDIA_MODULE_COMMAND_VIEWER) {
        int result = df_gvs_monitor_set_viewing(&module->monitor, generation,
            active, now_ms);

        if (result == DF_OK) df_media_module_sync_status_revision(module);
        return result;
    }
    return DF_ERR_INVALID;
}

int df_media_module_receive_control(struct df_media_module *module,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    struct df_gvs_monitor_result result;

    if (module == NULL || !module->initialized || frame == NULL) return DF_ERR_INVALID;
    if (df_gvs_monitor_receive(&module->monitor, frame, source_ipv4, now_ms,
            &result) != DF_OK) return DF_ERR_INVALID;
    if (result.confirmed) {
        df_media_module_sync_status_revision(module);
        return DF_OK;
    }
    if (result.failed) {
        df_media_module_set_failure(module, "monitor_unconfirmed");
        df_media_module_sync_status_revision(module);
        return DF_OK;
    }
    if (result.stopped) {
        module->preview_deadline_ms = 0U;
        if (df_media_module_cleanup_media(module) != DF_OK)
            return df_media_module_report_cleanup_failure(module,
                module->monitor.generation, now_ms);
        df_media_module_sync_status_revision(module);
        return DF_OK;
    }
    return DF_OK;
}

int df_media_module_push_jpeg(struct df_media_module *module,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    uint64_t generation, const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    struct df_gvs_monitor_result result;
    struct df_media_frame frame;

    if (module == NULL || !module->initialized || jpeg == NULL ||
        df_gvs_jpeg_validate(jpeg, length) != 0 || width == 0U || height == 0U ||
        df_gvs_monitor_admit_jpeg(&module->monitor, source, destination,
            source_ipv4, generation, timestamp_ms, &result) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_media_encoder_requires_restart(&module->encoder, width, height)) {
        if (df_media_module_cleanup_media(module) != DF_OK)
            return df_media_module_report_cleanup_failure(module,
                generation, timestamp_ms);
    }
    if (!module->queue_initialized) {
        if (df_media_frame_queue_init(&module->queue, generation,
                DF_GVS_VIDEO_MAX_FRAME) != DF_OK) return DF_ERR_IO;
        module->queue_initialized = true;
    }
    if (df_media_frame_queue_push(&module->queue, jpeg, length, generation,
            timestamp_ms) != DF_OK) return DF_ERR_IO;
    if (!df_media_encoder_is_running(&module->encoder)) {
        if (df_media_module_start_encoder(module, width, height) != DF_OK) {
            df_media_module_set_failure(module, "encoder_unavailable");
            df_media_module_sync_status_revision(module);
            return DF_ERR_IO;
        }
        if (module->monitor.state == DF_GVS_MONITOR_AWAITING_VIDEO) {
            if (df_gvs_monitor_mark_publishing(&module->monitor, generation,
                    timestamp_ms) != DF_OK) return DF_ERR_INVALID;
        }
    }
    if (df_media_frame_queue_pop(&module->queue, &frame) == DF_OK) {
        int status = df_media_encoder_write_frame(&module->encoder, &frame);
        if (status != DF_OK && status != DF_MEDIA_ENCODER_RETRY) {
            if (module->encoder.encoder_exited &&
                df_media_module_handle_encoder_exit(module, generation,
                    timestamp_ms) != DF_OK) return DF_ERR_IO;
            df_media_module_sync_status_revision(module);
            return status;
        }
    }
    if (module->encoder.encoder_exited &&
        df_media_module_handle_encoder_exit(module, generation,
            timestamp_ms) != DF_OK) return DF_ERR_IO;
    df_media_module_sync_status_revision(module);
    return DF_OK;
}

int df_media_module_preempt(struct df_media_module *module, uint64_t now_ms) {
    uint64_t generation;

    if (module == NULL || !module->initialized ||
        !df_media_module_active(module->monitor.state)) return DF_OK;
    generation = module->monitor.generation;
    if (df_gvs_monitor_stop(&module->monitor, generation, now_ms) != DF_OK)
        return DF_ERR_INVALID;
    if (df_media_module_tick(module, now_ms) != DF_OK) return DF_ERR_IO;
    if (df_media_module_cleanup_media(module) != DF_OK)
        return df_media_module_report_cleanup_failure(module, generation, now_ms);
    df_media_module_sync_status_revision(module);
    return DF_OK;
}

int df_media_module_tick(struct df_media_module *module, uint64_t now_ms) {
    struct df_gvs_monitor_action action;
    struct df_media_frame frame;
    enum df_gvs_monitor_state previous_state;
    uint64_t generation;

    if (module == NULL || !module->initialized) return DF_ERR_INVALID;
    previous_state = module->monitor.state;
    generation = module->monitor.generation;
    if (module->preview_deadline_ms != 0U &&
        now_ms >= module->preview_deadline_ms &&
        df_media_module_active(module->monitor.state) &&
        module->monitor.state != DF_GVS_MONITOR_STOPPING) {
        if (df_gvs_monitor_stop(&module->monitor, generation, now_ms) != DF_OK)
            return DF_ERR_INVALID;
        module->preview_deadline_ms = 0U;
    }
    if (df_gvs_monitor_step(&module->monitor, now_ms, &action) != DF_OK)
        return DF_ERR_INVALID;
    if (action.send && df_media_module_emit_action(module, &action) != DF_OK) {
        df_media_module_set_failure(module, "control_send_failed");
        df_media_module_sync_status_revision(module);
        return DF_ERR_IO;
    }
    if (module->monitor.state == DF_GVS_MONITOR_FAILED &&
        module->failure[0] == '\0') {
        df_media_module_set_failure(module, "monitor_failed");
    }
    if (previous_state == DF_GVS_MONITOR_STOPPING &&
        module->monitor.state == DF_GVS_MONITOR_IDLE &&
        module->monitor.failure == DF_GVS_MONITOR_STOP_TIMEOUT) {
        if (df_media_module_cleanup_media(module) != DF_OK)
            return df_media_module_report_cleanup_failure(module, generation, now_ms);
        df_media_module_set_failure(module, "stop_timeout");
    }
    if (df_media_encoder_is_running(&module->encoder)) {
        (void)df_media_encoder_tick(&module->encoder, now_ms);
        if (module->encoder.encoder_exited) {
            if (df_media_module_handle_encoder_exit(module, generation,
                    now_ms) != DF_OK) return DF_ERR_IO;
        } else if (df_media_frame_queue_pop(&module->queue, &frame) == DF_OK) {
            int status = df_media_encoder_write_frame(&module->encoder, &frame);
            if (status != DF_OK && status != DF_MEDIA_ENCODER_RETRY) {
                df_media_module_set_failure(module, "encoder_exited");
                if (module->encoder.encoder_exited &&
                    df_media_module_handle_encoder_exit(module, generation,
                        now_ms) != DF_OK) return DF_ERR_IO;
            }
        }
    }
    if (module->encoder.encoder_exited &&
        df_media_module_handle_encoder_exit(module, generation,
            now_ms) != DF_OK) return DF_ERR_IO;
    df_media_module_sync_status_revision(module);
    return DF_OK;
}

int df_media_module_status(const struct df_media_module *module,
    struct df_media_module_status *status) {
    if (module == NULL || status == NULL || !module->initialized)
        return DF_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    status->available = true;
    status->encoder_running = df_media_encoder_is_running(&module->encoder);
    status->monitor_state = module->monitor.state;
    (void)snprintf(status->state, sizeof(status->state), "%s",
        df_media_module_state_name(module->monitor.state));
    (void)snprintf(status->failure, sizeof(status->failure), "%s",
        module->failure);
    status->generation = module->monitor.generation;
    status->status_revision = module->status_revision;
    status->queue_drops = module->queue.dropped_oldest;
    return DF_OK;
}

int df_media_module_destroy(struct df_media_module *module) {
    int result;

    if (module == NULL) return DF_ERR_INVALID;
    result = df_media_module_cleanup_media(module);
    module->initialized = false;
    memset(module, 0, sizeof(*module));
    return result;
}

static void *df_media_module_api_create(
    const struct df_media_module_config_v2 *config,
    const struct df_media_module_callbacks_v2 *callbacks) {
    struct df_media_module *module = calloc(1U, sizeof(*module));

    if (module == NULL || df_media_module_init(module, config, callbacks, 0U) != DF_OK) {
        free(module);
        return NULL;
    }
    return module;
}

static void df_media_module_api_destroy(void *instance) {
    struct df_media_module *module = instance;
    if (module != NULL) {
        (void)df_media_module_destroy(module);
        free(module);
    }
}

static int df_media_module_api_start(void *instance, uint64_t now_ms) {
    return df_media_module_start(instance, now_ms);
}

static int df_media_module_api_command(void *instance,
    enum df_media_module_command command, uint64_t generation, bool active,
    uint64_t now_ms) {
    return df_media_module_command(instance, command, generation, active, now_ms);
}

static int df_media_module_api_receive_control(void *instance,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    return df_media_module_receive_control(instance, frame, source_ipv4, now_ms);
}

static int df_media_module_api_push_jpeg(void *instance,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    uint64_t generation, const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    return df_media_module_push_jpeg(instance, source, destination, source_ipv4,
        generation, jpeg, length, width, height, timestamp_ms);
}

static int df_media_module_api_preempt(void *instance, uint64_t now_ms) {
    return df_media_module_preempt(instance, now_ms);
}

static int df_media_module_api_tick(void *instance, uint64_t now_ms) {
    return df_media_module_tick(instance, now_ms);
}

static int df_media_module_api_status(const void *instance,
    struct df_media_module_status *status) {
    return df_media_module_status(instance, status);
}

const struct df_media_module_api_v2 df_media_module_api_v2 = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION_V2,
    .struct_size = sizeof(struct df_media_module_api_v2),
    .create = df_media_module_api_create,
    .destroy = df_media_module_api_destroy,
    .start = df_media_module_api_start,
    .command = df_media_module_api_command,
    .receive_control = df_media_module_api_receive_control,
    .push_jpeg = df_media_module_api_push_jpeg,
    .preempt = df_media_module_api_preempt,
    .tick = df_media_module_api_tick,
    .status = df_media_module_api_status,
};
