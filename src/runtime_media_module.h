#ifndef DOORFAST_RUNTIME_MEDIA_MODULE_H
#define DOORFAST_RUNTIME_MEDIA_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "media_module.h"

#define DF_RUNTIME_MEDIA_MODULE_PATH "/usr/lib/doorfast/media-v2.so"

struct df_runtime_media_module {
    void *handle;
    const struct df_media_module_api_v2 *api;
    void *instance;
    bool available;
};

int df_runtime_media_module_validate_api_v3(
    const struct df_media_module_api_v3 *);

int df_runtime_media_module_start(struct df_runtime_media_module *,
    const struct df_media_module_config_v2 *,
    const struct df_media_module_callbacks_v2 *);
int df_runtime_media_module_start_with_api(struct df_runtime_media_module *,
    const struct df_media_module_api_v2 *,
    const struct df_media_module_config_v2 *,
    const struct df_media_module_callbacks_v2 *);
int df_runtime_media_module_request_start(struct df_runtime_media_module *,
    uint64_t now_ms);
int df_runtime_media_module_command(struct df_runtime_media_module *,
    enum df_media_module_command, uint64_t generation, bool active,
    uint64_t now_ms);
int df_runtime_media_module_receive_control(struct df_runtime_media_module *,
    const struct df_gvs_frame *, uint32_t source_ipv4, uint64_t now_ms);
int df_runtime_media_module_push_jpeg(struct df_runtime_media_module *,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    uint64_t generation, const uint8_t *, size_t, uint16_t, uint16_t,
    uint64_t timestamp_ms);
int df_runtime_media_module_preempt(struct df_runtime_media_module *,
    uint64_t now_ms);
int df_runtime_media_module_tick(struct df_runtime_media_module *,
    uint64_t now_ms);
int df_runtime_media_module_status(const struct df_runtime_media_module *,
    struct df_media_module_status *);
void df_runtime_media_module_stop(struct df_runtime_media_module *);

#endif
