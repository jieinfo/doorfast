#include "runtime_media_module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int df_runtime_media_module_validate_api_v3(
    const struct df_media_module_api_v3 *api) {
    if (api == NULL || api->abi_version != DF_MEDIA_MODULE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->create == NULL ||
        api->destroy == NULL || api->start == NULL || api->command == NULL ||
        api->receive_control == NULL || api->push_jpeg == NULL ||
        api->push_video == NULL || api->tick == NULL || api->status == NULL)
        return DF_ERR_INVALID;
    return DF_OK;
}

static const struct df_media_module_diagnostics_api_v1 *
df_runtime_media_module_resolve_diagnostics(void *handle) {
    const struct df_media_module_diagnostics_api_v1 *api = NULL;

    if (handle == NULL) return NULL;
    *(void **)(&api) = dlsym(handle, "df_media_module_diagnostics_api_v1");
    if (api == NULL || api->abi_version !=
            DF_MEDIA_MODULE_DIAGNOSTICS_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->status == NULL)
        return NULL;
    return api;
}

static bool df_runtime_media_diagnostics_v2_valid(
    const struct df_media_module_diagnostics_api_v2 *api) {
    return api != NULL &&
        api->abi_version == DF_MEDIA_MODULE_DIAGNOSTICS_ABI_VERSION_V2 &&
        api->struct_size == sizeof(*api) && api->status != NULL;
}

static const struct df_media_module_diagnostics_api_v2 *
df_runtime_media_module_resolve_diagnostics_v2(void *handle) {
    const struct df_media_module_diagnostics_api_v2 *api = NULL;
    if (handle == NULL) return NULL;
    *(void **)(&api) = dlsym(handle, "df_media_module_diagnostics_api_v2");
    return df_runtime_media_diagnostics_v2_valid(api) ? api : NULL;
}

static const struct df_media_module_outbound_upgrade_api_v1 *
df_runtime_media_module_resolve_outbound_upgrade(void *handle) {
    const struct df_media_module_outbound_upgrade_api_v1 *api = NULL;
    if (handle == NULL) return NULL;
    *(void **)(&api) = dlsym(handle, "df_media_module_outbound_upgrade_api_v1");
    if (api == NULL || api->abi_version !=
            DF_MEDIA_MODULE_OUTBOUND_UPGRADE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->operation == NULL)
        return NULL;
    return api;
}

int df_runtime_media_module_start_with_api(struct df_runtime_media_module *module,
    const struct df_media_module_api_v3 *api,
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    void *handle;

    if (module == NULL || config == NULL || callbacks == NULL ||
        df_runtime_media_module_validate_api_v3(api) != DF_OK)
        return DF_ERR_INVALID;
    handle = module->handle;
    memset(module, 0, sizeof(*module));
    module->handle = handle;
    module->api = api;
    module->instance = api->create(config, callbacks);
    if (module->instance == NULL) {
        module->api = NULL;
        return DF_ERR_IO;
    }
    if (config->station_count > 0U) {
        module->session_snapshot = calloc(config->station_count,
            sizeof(*module->session_snapshot));
        if (module->session_snapshot == NULL) {
            api->destroy(module->instance);
            module->instance = NULL;
            module->api = NULL;
            return DF_ERR_IO;
        }
        module->session_snapshot_capacity = config->station_count;
    }
    module->available = true;
    return DF_OK;
}

int df_runtime_media_module_start(struct df_runtime_media_module *module,
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    const struct df_media_module_api_v3 *api;
    void *handle;
    int result;

    if (module == NULL || config == NULL || callbacks == NULL) return DF_ERR_INVALID;
    memset(module, 0, sizeof(*module));
    handle = dlopen(DF_RUNTIME_MEDIA_MODULE_PATH, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) return DF_ERR_IO;
    *(void **)(&api) = dlsym(handle, "df_media_module_api_v3");
    result = df_runtime_media_module_validate_api_v3(api);
    if (result != DF_OK) {
        (void)dlclose(handle);
        return DF_ERR_INVALID;
    }
    module->handle = handle;
    result = df_runtime_media_module_start_with_api(module, api, config, callbacks);
    if (result != DF_OK) {
        module->handle = NULL;
        (void)dlclose(handle);
    } else {
        module->diagnostics_api =
            df_runtime_media_module_resolve_diagnostics(handle);
        module->diagnostics_api_v2 =
            df_runtime_media_module_resolve_diagnostics_v2(handle);
        module->outbound_upgrade_api =
            df_runtime_media_module_resolve_outbound_upgrade(handle);
    }
    return result;
}

static int df_runtime_media_module_available(
    const struct df_runtime_media_module *module) {
    return module != NULL && module->available && module->api != NULL &&
        module->instance != NULL ? DF_OK : DF_ERR_INVALID;
}

int df_runtime_media_module_request_start(struct df_runtime_media_module *module,
    const char *station_id, uint64_t now_ms, uint64_t *generation) {
    struct df_media_module_status_v3 status = {0};
    size_t index;
    int result;

    if (generation == NULL) return DF_ERR_INVALID;
    *generation = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        station_id == NULL || station_id[0] == '\0' ||
        module->session_snapshot == NULL ||
        module->session_snapshot_capacity == 0U) return DF_ERR_INVALID;
    result = module->api->start(module->instance, station_id,
        DF_MEDIA_SESSION_PREVIEW, 0U, now_ms);
    if (result != DF_OK) return result;
    status.sessions = module->session_snapshot;
    status.session_count = module->session_snapshot_capacity;
    result = module->api->status(module->instance, &status);
    if (result == DF_OK &&
        (status.session_count > module->session_snapshot_capacity ||
         status.required_session_count > module->session_snapshot_capacity))
        result = DF_ERR_IO;
    if (result == DF_OK) {
        result = DF_ERR_IO;
        for (index = 0U; index < status.session_count; index++) {
            if (module->session_snapshot[index].active &&
                strcmp(module->session_snapshot[index].station_id,
                    station_id) == 0) {
                *generation = module->session_snapshot[index].generation;
                result = *generation == 0U ? DF_ERR_IO : DF_OK;
                break;
            }
        }
    }
    return result;
}

int df_runtime_media_module_call(struct df_runtime_media_module *module,
    const char *station_id, uint64_t now_ms, uint64_t *generation) {
    struct df_media_module_status_v3 status = {0};
    size_t index;
    int result;

    if (generation == NULL) return DF_ERR_INVALID;
    *generation = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        station_id == NULL || station_id[0] == '\0' ||
        module->session_snapshot == NULL || module->session_snapshot_capacity == 0U)
        return DF_ERR_INVALID;
    if (module->outbound_upgrade_api == NULL)
        return DF_ERR_INVALID;
    status.sessions = module->session_snapshot;
    status.session_count = module->session_snapshot_capacity;
    result = module->api->status(module->instance, &status);
    if (result != DF_OK || status.session_count > module->session_snapshot_capacity)
        return DF_ERR_IO;
    for (index = 0U; index < status.session_count; index++) {
        if (status.sessions[index].active &&
            strcmp(status.sessions[index].station_id, station_id) == 0) {
            struct df_media_session_key key = {.station_id = station_id,
                .generation = status.sessions[index].generation};
            result = df_runtime_media_module_call_key(module, &key, now_ms);
            if (result == DF_OK) *generation = key.generation;
            return result;
        }
    }
    return DF_ERR_IO;
}

int df_runtime_media_module_set_outbound_api(struct df_runtime_media_module *module,
    const struct df_media_module_outbound_upgrade_api_v1 *api) {
    if (module == NULL) return DF_ERR_INVALID;
    module->outbound_upgrade_api = NULL;
    if (api == NULL || api->abi_version !=
            DF_MEDIA_MODULE_OUTBOUND_UPGRADE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->operation == NULL)
        return DF_ERR_INVALID;
    module->outbound_upgrade_api = api;
    return DF_OK;
}

int df_runtime_media_module_outbound_operation(struct df_runtime_media_module *module,
    enum df_media_outbound_operation operation,
    const struct df_media_session_key *key, uint64_t control_generation,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK ||
        module->outbound_upgrade_api == NULL || key == NULL)
        return DF_ERR_INVALID;
    return module->outbound_upgrade_api->operation(module->instance, operation,
        key, control_generation, now_ms);
}

int df_runtime_media_module_call_key(struct df_runtime_media_module *module,
    const struct df_media_session_key *key, uint64_t now_ms) {
    int result = df_runtime_media_module_outbound_operation(module,
        DF_MEDIA_OUTBOUND_CHECK, key, 0U, now_ms);
    if (result != DF_OK) return result;
    return module->api->start(module->instance, key->station_id,
        DF_MEDIA_SESSION_CALL, 0U, now_ms);
}

int df_runtime_media_module_incoming_call(
    struct df_runtime_media_module *module, const char *station_id,
    uint64_t generation, uint64_t now_ms,
    char preempted_station_id[DF_MEDIA_MODULE_STATION_ID_MAX],
    uint64_t *preempted_generation) {
    struct df_media_module_status_v3 status = {0};
    int result;

    if (preempted_station_id != NULL) preempted_station_id[0] = '\0';
    if (preempted_generation != NULL) *preempted_generation = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        station_id == NULL || station_id[0] == '\0' || generation == 0U ||
        preempted_station_id == NULL || preempted_generation == NULL)
        return DF_ERR_INVALID;
    result = module->api->start(module->instance, station_id,
        DF_MEDIA_SESSION_CALL, generation, now_ms);
    if (result != DF_OK) return result;
    status.sessions = module->session_snapshot;
    status.session_count = module->session_snapshot_capacity;
    result = module->api->status(module->instance, &status);
    if (result != DF_OK ||
        status.session_count > module->session_snapshot_capacity ||
        status.required_session_count > module->session_snapshot_capacity)
        return DF_ERR_IO;
    (void)snprintf(preempted_station_id, DF_MEDIA_MODULE_STATION_ID_MAX,
        "%s", status.preempted_station_id);
    *preempted_generation = status.preempted_generation;
    return DF_OK;
}

int df_runtime_media_module_command(struct df_runtime_media_module *module,
    enum df_media_module_command command, const struct df_media_session_key *key,
    bool active,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK || key == NULL)
        return DF_ERR_INVALID;
    return module->api->command(module->instance, command, key, active, now_ms);
}

int df_runtime_media_module_receive_control(struct df_runtime_media_module *module,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->receive_control(module->instance, frame, source_ipv4, now_ms);
}

int df_runtime_media_module_push_jpeg(struct df_runtime_media_module *module,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->push_jpeg(module->instance, source, destination,
        source_ipv4, jpeg, length, width, height, timestamp_ms);
}

int df_runtime_media_module_push_video(struct df_runtime_media_module *module,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4,
    uint64_t timestamp_ms) {
    if (df_runtime_media_module_available(module) != DF_OK)
        return DF_ERR_INVALID;
    return module->api->push_video(module->instance, packet, source_ipv4,
        timestamp_ms);
}

int df_runtime_media_module_tick(struct df_runtime_media_module *module,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->tick(module->instance, now_ms);
}

int df_runtime_media_module_status(const struct df_runtime_media_module *module,
    struct df_media_module_status_v3 *status) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->status(module->instance, status);
}

int df_runtime_media_module_diagnostics(
    const struct df_runtime_media_module *module,
    struct df_media_session_diagnostics_v1 *entries, size_t capacity,
    size_t *count) {
    size_t required = 0U;
    size_t copied = capacity;

    if (count == NULL) return DF_ERR_INVALID;
    *count = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        module->diagnostics_api == NULL) return DF_ERR_INVALID;
    if (entries == NULL) copied = 0U;
    if (module->diagnostics_api->status(module->instance, entries, &copied,
            &required) != DF_OK) return DF_ERR_IO;
    if (required > capacity) return DF_ERR_IO;
    *count = copied;
    return DF_OK;
}

const char *df_media_timer_name(enum df_media_timer value) {
    switch (value) {
    case DF_MEDIA_TIMER_NONE: return "none";
    case DF_MEDIA_TIMER_CONFIRMATION: return "confirmation";
    case DF_MEDIA_TIMER_SESSION: return "session";
    case DF_MEDIA_TIMER_FIRST_FRAME: return "first_frame";
    case DF_MEDIA_TIMER_RECONNECT: return "reconnect";
    default: return "unknown";
    }
}

const char *df_media_end_reason_name(enum df_media_end_reason value) {
    switch (value) {
    case DF_MEDIA_END_NONE: return "none";
    case DF_MEDIA_END_LOCAL_HANGUP: return "local_hangup";
    case DF_MEDIA_END_PEER_HANGUP: return "peer_hangup";
    case DF_MEDIA_END_SESSION_TIMEOUT: return "session_timeout";
    case DF_MEDIA_END_CONFIRMATION_TIMEOUT: return "confirmation_timeout";
    case DF_MEDIA_END_PREEMPTED: return "preempted";
    case DF_MEDIA_END_RUNTIME_RESET: return "runtime_reset";
    default: return "unknown";
    }
}

const char *df_media_failure_reason_name(enum df_media_failure_reason value) {
    switch (value) {
    case DF_MEDIA_FAILURE_NONE: return "none";
    case DF_MEDIA_FAILURE_DISPATCH_FAILED: return "dispatch_failed";
    case DF_MEDIA_FAILURE_DISPATCH_TIMEOUT: return "dispatch_timeout";
    case DF_MEDIA_FAILURE_CONFIRMATION_REJECTED: return "confirmation_rejected";
    case DF_MEDIA_FAILURE_CONFIRMATION_TIMEOUT: return "confirmation_timeout";
    case DF_MEDIA_FAILURE_FIRST_FRAME_TIMEOUT: return "first_frame_timeout";
    case DF_MEDIA_FAILURE_SOURCE_STALLED: return "source_stalled";
    case DF_MEDIA_FAILURE_ENCODER_FAILED: return "encoder_failed";
    case DF_MEDIA_FAILURE_PUBLISHER_FAILED: return "publisher_failed";
    case DF_MEDIA_FAILURE_CAPACITY_BUSY: return "capacity_busy";
    case DF_MEDIA_FAILURE_PREEMPTED: return "preempted";
    case DF_MEDIA_FAILURE_CANCELLED: return "cancelled";
    case DF_MEDIA_FAILURE_RUNTIME_RESET: return "runtime_reset";
    default: return "unknown";
    }
}

static bool df_runtime_media_diagnostic_entry_valid(
    const struct df_media_session_diagnostics_v2 *entry, bool terminal) {
    return entry->station_id[0] != '\0' &&
        memchr(entry->station_id, '\0', sizeof(entry->station_id)) != NULL &&
        entry->generation != 0U &&
        entry->timer >= DF_MEDIA_TIMER_NONE &&
        entry->timer <= DF_MEDIA_TIMER_UNKNOWN &&
        entry->end_reason >= DF_MEDIA_END_NONE &&
        entry->end_reason <= DF_MEDIA_END_UNKNOWN &&
        entry->failure_reason >= DF_MEDIA_FAILURE_NONE &&
        entry->failure_reason <= DF_MEDIA_FAILURE_UNKNOWN &&
        (entry->timer != DF_MEDIA_TIMER_NONE || entry->remaining_ms == 0U) &&
        (terminal ? (entry->end_reason != DF_MEDIA_END_NONE &&
                     entry->timer == DF_MEDIA_TIMER_NONE &&
                     entry->remaining_ms == 0U) :
                    entry->end_reason == DF_MEDIA_END_NONE);
}

int df_runtime_media_module_diagnostics_v2(
    const struct df_runtime_media_module *module, uint64_t now_ms,
    struct df_media_module_diagnostics_v2 *status) {
    struct df_media_module_diagnostics_v2 next;
    size_t capacity, last_capacity;
    size_t index;

    if (status == NULL) return DF_ERR_INVALID;
    next = *status;
    capacity = status->sessions != NULL ? status->session_count : 0U;
    last_capacity = status->last_results != NULL ? status->last_result_count : 0U;
    status->session_count = status->required_session_count = 0U;
    status->last_result_count = status->required_last_result_count = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        !df_runtime_media_diagnostics_v2_valid(module->diagnostics_api_v2))
        return DF_ERR_INVALID;
    if (module->diagnostics_api_v2->status(module->instance, now_ms, &next) !=
            DF_OK || next.sessions != status->sessions ||
        next.last_results != status->last_results ||
        next.session_count > capacity || next.required_session_count > capacity ||
        next.session_count != next.required_session_count ||
        next.last_result_count > last_capacity ||
        next.required_last_result_count > last_capacity ||
        next.last_result_count != next.required_last_result_count)
        return DF_ERR_IO;
    for (index = 0U; index < next.session_count; index++)
        if (!df_runtime_media_diagnostic_entry_valid(&next.sessions[index],
                false)) return DF_ERR_IO;
    for (index = 0U; index < next.last_result_count; index++)
        if (!df_runtime_media_diagnostic_entry_valid(&next.last_results[index],
                true)) return DF_ERR_IO;
    *status = next;
    return DF_OK;
}

void df_runtime_media_module_stop(struct df_runtime_media_module *module) {
    if (module == NULL) return;
    if (module->available && module->api != NULL && module->instance != NULL)
        module->api->destroy(module->instance);
    free(module->session_snapshot);
    if (module->handle != NULL) (void)dlclose(module->handle);
    memset(module, 0, sizeof(*module));
}
