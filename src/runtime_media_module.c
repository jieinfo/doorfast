#include "runtime_media_module.h"

#include <dlfcn.h>
#include <string.h>

static int df_runtime_media_module_api_valid(
    const struct df_media_module_api_v1 *api) {
    if (api == NULL || api->abi_version != DF_MEDIA_MODULE_ABI_VERSION ||
        api->struct_size != sizeof(*api) || api->create == NULL ||
        api->destroy == NULL || api->start == NULL || api->command == NULL ||
        api->receive_control == NULL || api->push_jpeg == NULL ||
        api->preempt == NULL || api->tick == NULL || api->status == NULL) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_runtime_media_module_start_with_api(struct df_runtime_media_module *module,
    const struct df_media_module_api_v1 *api,
    const struct df_media_module_config_v1 *config,
    const struct df_media_module_callbacks_v1 *callbacks) {
    void *handle;

    if (module == NULL || config == NULL || callbacks == NULL ||
        df_runtime_media_module_api_valid(api) != DF_OK) return DF_ERR_INVALID;
    handle = module->handle;
    memset(module, 0, sizeof(*module));
    module->handle = handle;
    module->api = api;
    module->instance = api->create(config, callbacks);
    if (module->instance == NULL) {
        module->api = NULL;
        return DF_ERR_IO;
    }
    module->available = true;
    return DF_OK;
}

int df_runtime_media_module_start(struct df_runtime_media_module *module,
    const struct df_media_module_config_v1 *config,
    const struct df_media_module_callbacks_v1 *callbacks) {
    const struct df_media_module_api_v1 *api;
    void *handle;
    int result;

    if (module == NULL || config == NULL || callbacks == NULL) return DF_ERR_INVALID;
    memset(module, 0, sizeof(*module));
    handle = dlopen(DF_RUNTIME_MEDIA_MODULE_PATH, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) return DF_ERR_IO;
    *(void **)(&api) = dlsym(handle, "df_media_module_api_v1");
    result = df_runtime_media_module_api_valid(api);
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
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->start(module->instance, now_ms);
}

int df_runtime_media_module_command(struct df_runtime_media_module *module,
    enum df_media_module_command command, uint64_t generation, bool active,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->command(module->instance, command, generation, active, now_ms);
}

int df_runtime_media_module_receive_control(struct df_runtime_media_module *module,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->receive_control(module->instance, frame, source_ipv4, now_ms);
}

int df_runtime_media_module_push_jpeg(struct df_runtime_media_module *module,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    uint64_t generation, const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->push_jpeg(module->instance, source, destination,
        source_ipv4, generation, jpeg, length, width, height, timestamp_ms);
}

int df_runtime_media_module_preempt(struct df_runtime_media_module *module,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->preempt(module->instance, now_ms);
}

int df_runtime_media_module_tick(struct df_runtime_media_module *module,
    uint64_t now_ms) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->tick(module->instance, now_ms);
}

int df_runtime_media_module_status(const struct df_runtime_media_module *module,
    struct df_media_module_status *status) {
    if (df_runtime_media_module_available(module) != DF_OK) return DF_ERR_INVALID;
    return module->api->status(module->instance, status);
}

void df_runtime_media_module_stop(struct df_runtime_media_module *module) {
    if (module == NULL) return;
    if (module->available && module->api != NULL && module->instance != NULL)
        module->api->destroy(module->instance);
    if (module->handle != NULL) (void)dlclose(module->handle);
    memset(module, 0, sizeof(*module));
}
