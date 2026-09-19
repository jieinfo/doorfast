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

void df_runtime_media_module_stop(struct df_runtime_media_module *module) {
    if (module == NULL) return;
    if (module->available && module->api != NULL && module->instance != NULL)
        module->api->destroy(module->instance);
    free(module->session_snapshot);
    if (module->handle != NULL) (void)dlclose(module->handle);
    memset(module, 0, sizeof(*module));
}
