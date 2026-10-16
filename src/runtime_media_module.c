#include "runtime_media_module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int df_runtime_media_module_available(
    const struct df_runtime_media_module *module);

static int adapted_emit(const uint8_t to[6], uint32_t ip, const uint8_t from[6],
    uint8_t family, uint8_t opcode, const uint8_t *payload, size_t size, void *context) {
    struct df_runtime_media_module *m = context;
    if (m->suppress_stop != NULL && family == 3 && opcode == 2 &&
        memcmp(from, m->local, 6) == 0 && memcmp(to, m->suppress_stop->peer, 6) == 0)
        return DF_OK;
    return m->original_callbacks.emit_control(to, ip, from, family, opcode,
        payload, size, m->original_callbacks.context);
}
static int adapted_route(const uint8_t peer[6], uint64_t now, uint32_t *ip, void *context) {
    struct df_runtime_media_module *m = context;
    return m->original_callbacks.resolve_route(peer, now, ip, m->original_callbacks.context);
}
static int adapted_memory(uint64_t *size, void *context) {
    struct df_runtime_media_module *m = context;
    return m->original_callbacks.available_memory(size, m->original_callbacks.context);
}

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

static const struct df_media_module_unlock_prepare_api_v1 *
df_runtime_media_module_resolve_unlock_prepare(void *handle, bool *invalid) {
    const struct df_media_module_unlock_prepare_api_v1 *api = NULL;

    if (handle == NULL) return NULL;
    *(void **)(&api) = dlsym(handle,
        "df_media_module_unlock_prepare_api_v1");
    *invalid = api != NULL;
    if (api == NULL || api->abi_version !=
            DF_MEDIA_MODULE_UNLOCK_PREPARE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->start == NULL ||
        api->status == NULL || api->stop == NULL)
        return NULL;
    *invalid = false;
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
    module->original_callbacks = *callbacks;
    module->adapted_callbacks = (struct df_media_module_callbacks_v3){
        .emit_control = callbacks->emit_control != NULL ? adapted_emit : NULL,
        .resolve_route = callbacks->resolve_route != NULL ? adapted_route : NULL,
        .available_memory = callbacks->available_memory != NULL ? adapted_memory : NULL,
        .context = module};
    memcpy(module->local, config->local, 6);
    if (config->station_count != 0 && config->stations != NULL) {
        module->incoming_bindings = calloc(config->station_count, sizeof(*module->incoming_bindings));
        if (module->incoming_bindings == NULL) return DF_ERR_IO;
        module->incoming_binding_count = config->station_count;
        for (size_t i = 0; i < config->station_count; i++) {
            (void)snprintf(module->incoming_bindings[i].station_id,
                sizeof(module->incoming_bindings[i].station_id), "%s", config->stations[i].id);
            memcpy(module->incoming_bindings[i].peer, config->stations[i].logical_address, 6);
        }
    }
    module->instance = api->create(config, &module->adapted_callbacks);
    if (module->instance == NULL) {
        free(module->incoming_bindings);
        module->incoming_bindings = NULL;
        module->incoming_binding_count = 0;
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
            free(module->incoming_bindings);
            module->incoming_bindings = NULL;
            module->incoming_binding_count = 0;
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
        module->unlock_prepare_api =
            df_runtime_media_module_resolve_unlock_prepare(handle,
                &module->unlock_prepare_invalid);
        const struct df_media_module_lifecycle_api_v2 *lifecycle = NULL;
        *(void **)(&lifecycle) = dlsym(handle, "df_media_module_lifecycle_api_v2");
        (void)df_runtime_media_module_set_lifecycle_api(module, lifecycle);
    }
    return result;
}

int df_runtime_media_module_set_unlock_prepare_api(
    struct df_runtime_media_module *module,
    const struct df_media_module_unlock_prepare_api_v1 *api) {
    if (module == NULL) return DF_ERR_INVALID;
    module->unlock_prepare_api = NULL;
    module->unlock_prepare_invalid = api != NULL;
    if (api == NULL || api->abi_version !=
            DF_MEDIA_MODULE_UNLOCK_PREPARE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->start == NULL ||
        api->status == NULL || api->stop == NULL)
        return DF_ERR_INVALID;
    module->unlock_prepare_api = api;
    module->unlock_prepare_invalid = false;
    return DF_OK;
}

int df_runtime_media_module_unlock_prepare_start(
    struct df_runtime_media_module *module, const char *station_id,
    uint64_t now_ms, uint64_t *generation) {
    if (generation == NULL) return DF_ERR_INVALID;
    *generation = 0U;
    if (df_runtime_media_module_available(module) != DF_OK ||
        module->unlock_prepare_api == NULL || station_id == NULL ||
        station_id[0] == '\0') return DF_ERR_INVALID;
    return module->unlock_prepare_api->start(module->instance, station_id,
        now_ms, generation);
}

int df_runtime_media_module_unlock_prepare_status(
    const struct df_runtime_media_module *module, const char *station_id,
    uint64_t generation, struct df_media_unlock_prepare_status_v1 *status) {
    if (status == NULL) return DF_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    if (df_runtime_media_module_available(module) != DF_OK ||
        module->unlock_prepare_api == NULL || station_id == NULL ||
        station_id[0] == '\0' || generation == 0U)
        return DF_ERR_INVALID;
    return module->unlock_prepare_api->status(module->instance, station_id,
        generation, status);
}

int df_runtime_media_module_unlock_prepare_stop(
    struct df_runtime_media_module *module,
    const struct df_media_session_key *key, uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK ||
        module->unlock_prepare_api == NULL || key == NULL ||
        key->station_id == NULL || key->generation == 0U)
        return DF_ERR_INVALID;
    return module->unlock_prepare_api->stop(module->instance, key, now_ms);
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
        (module->outbound_upgrade_api == NULL && module->lifecycle_api == NULL) || key == NULL)
        return DF_ERR_INVALID;
    if (module->lifecycle_api != NULL)
        return module->lifecycle_api->operation(module->instance, operation,
            key, control_generation, now_ms);
    if (operation > DF_MEDIA_OUTBOUND_MARK_SENT) return DF_ERR_INVALID;
    return module->outbound_upgrade_api->operation(module->instance, operation,
        key, control_generation, now_ms);
}

int df_runtime_media_module_set_lifecycle_api(struct df_runtime_media_module *module,
    const struct df_media_module_lifecycle_api_v2 *api) {
    if (module == NULL) return DF_ERR_INVALID;
    module->lifecycle_api = NULL;
    if (api == NULL || api->abi_version != DF_MEDIA_MODULE_LIFECYCLE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->operation == NULL) return DF_ERR_INVALID;
    module->lifecycle_api = api;
    return DF_OK;
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
    for (size_t i = 0; i < module->incoming_binding_count; i++) {
        struct df_runtime_incoming_media_binding *b = &module->incoming_bindings[i];
        if (strcmp(b->station_id, station_id) != 0) continue;
        for (size_t j = 0; j < status.session_count; j++) {
            const struct df_media_session_status_v3 *s = &status.sessions[j];
            if (!s->active || s->purpose != DF_MEDIA_SESSION_CALL ||
                strcmp(s->station_id, station_id) != 0) continue;
            b->control_generation = generation;
            b->media_generation = s->generation;
            b->cleanup_pending = false;
            b->cleanup_error = DF_OK;
            b->retry_ms = 0;
        }
    }
    (void)snprintf(preempted_station_id, DF_MEDIA_MODULE_STATION_ID_MAX,
        "%s", status.preempted_station_id);
    *preempted_generation = status.preempted_generation;
    return DF_OK;
}

static int retry_legacy_incoming(struct df_runtime_media_module *m,
    struct df_runtime_incoming_media_binding *b, uint64_t now) {
    struct df_media_module_status_v3 status = {.sessions = m->session_snapshot,
        .session_count = m->session_snapshot_capacity};
    int result = m->api->status(m->instance, &status);
    if (result != DF_OK || status.session_count > m->session_snapshot_capacity ||
        status.required_session_count > m->session_snapshot_capacity) {
        b->cleanup_error = DF_ERR_IO;
        return DF_ERR_IO;
    }
    bool found = false;
    for (size_t i = 0; i < status.session_count; i++) {
        const struct df_media_session_status_v3 *s = &status.sessions[i];
        if (s->active && strcmp(s->station_id, b->station_id) == 0) {
            if (s->generation != b->media_generation || s->purpose != DF_MEDIA_SESSION_CALL) {
                b->cleanup_pending = false;
                b->cleanup_error = DF_MEDIA_ERROR_GENERATION_MISMATCH;
                return b->cleanup_error;
            }
            found = true;
        }
    }
    if (!found) { b->cleanup_pending = false; return DF_OK; }
    if (now < b->retry_ms) return b->cleanup_error;
    const struct df_media_session_key key = {b->station_id, b->media_generation};
    m->suppress_stop = b;
    result = m->api->command(m->instance, DF_MEDIA_MODULE_COMMAND_STOP, &key, false, now);
    m->suppress_stop = NULL;
    b->cleanup_error = result;
    b->retry_ms = now <= UINT64_MAX - 100U ? now + 100U : UINT64_MAX;
    return result;
}

int df_runtime_media_module_release_incoming(struct df_runtime_media_module *m,
    const char *station, uint64_t control, enum df_media_outbound_operation operation,
    uint64_t now) {
    if (df_runtime_media_module_available(m) != DF_OK || station == NULL || control == 0 ||
        operation < DF_MEDIA_INCOMING_RELEASE_LOCAL || operation > DF_MEDIA_INCOMING_RELEASE_UNKNOWN)
        return DF_ERR_INVALID;
    for (size_t i = 0; i < m->incoming_binding_count; i++) {
        struct df_runtime_incoming_media_binding *b = &m->incoming_bindings[i];
        if (strcmp(station, b->station_id) != 0) continue;
        if (control != b->control_generation || b->media_generation == 0)
            return DF_MEDIA_ERROR_GENERATION_MISMATCH;
        const struct df_media_session_key key = {b->station_id, b->media_generation};
        if (m->lifecycle_api != NULL) {
            b->cleanup_error = df_runtime_media_module_outbound_operation(m, operation,
                &key, control, now);
            return b->cleanup_error;
        }
        b->cleanup_pending = true;
        return retry_legacy_incoming(m, b, now);
    }
    return DF_ERR_INVALID;
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
    int result = module->api->tick(module->instance, now_ms);
    for (size_t i = 0; i < module->incoming_binding_count; i++)
        if (module->incoming_bindings[i].cleanup_pending &&
            retry_legacy_incoming(module, &module->incoming_bindings[i], now_ms) != DF_OK)
            result = DF_ERR_IO;
    return result;
}

int df_runtime_media_module_status(const struct df_runtime_media_module *module,
    struct df_media_module_status_v3 *status) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    size_t capacity = status != NULL ? status->session_count : 0;
    int result = module->api->status(module->instance, status);
    if (result != DF_OK || status == NULL || status->session_count > capacity)
        return result != DF_OK ? result : DF_ERR_IO;
    for (size_t i = 0; i < module->incoming_binding_count; i++) {
        const struct df_runtime_incoming_media_binding *b = &module->incoming_bindings[i];
        if (!b->cleanup_pending || b->cleanup_error == DF_OK) continue;
        for (size_t j = 0; status->sessions != NULL && j < status->session_count; j++) {
            struct df_media_session_status_v3 *s = &status->sessions[j];
            if (s->generation == b->media_generation && strcmp(s->station_id, b->station_id) == 0) {
                s->last_error = DF_MEDIA_ERROR_ENCODER_FAILED;
                s->state = DF_MEDIA_SESSION_STOPPING;
                s->ready = false;
            }
        }
    }
    return result;
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
    free(module->incoming_bindings);
    if (module->handle != NULL) (void)dlclose(module->handle);
    memset(module, 0, sizeof(*module));
}
