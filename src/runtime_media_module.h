#ifndef DOORFAST_RUNTIME_MEDIA_MODULE_H
#define DOORFAST_RUNTIME_MEDIA_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "media_module.h"

#define DF_RUNTIME_MEDIA_MODULE_PATH "/usr/lib/doorfast/media-v3.so"

struct df_runtime_media_module {
    void *handle;
    const struct df_media_module_api_v3 *api;
    void *instance;
    struct df_media_session_status_v3 *session_snapshot;
    size_t session_snapshot_capacity;
    char reported_failed_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t reported_failed_generation;
    bool available;
};

int df_runtime_media_module_validate_api_v3(
    const struct df_media_module_api_v3 *);

int df_runtime_media_module_start(struct df_runtime_media_module *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
int df_runtime_media_module_start_with_api(struct df_runtime_media_module *,
    const struct df_media_module_api_v3 *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
int df_runtime_media_module_request_start(struct df_runtime_media_module *,
    const char *station_id, uint64_t now_ms, uint64_t *generation);
int df_runtime_media_module_incoming_call(struct df_runtime_media_module *,
    const char *station_id, uint64_t generation, uint64_t now_ms,
    char preempted_station_id[DF_MEDIA_MODULE_STATION_ID_MAX],
    uint64_t *preempted_generation);
int df_runtime_media_module_command(struct df_runtime_media_module *,
    enum df_media_module_command, const struct df_media_session_key *, bool active,
    uint64_t now_ms);
int df_runtime_media_module_receive_control(struct df_runtime_media_module *,
    const struct df_gvs_frame *, uint32_t source_ipv4, uint64_t now_ms);
int df_runtime_media_module_push_jpeg(struct df_runtime_media_module *,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    const uint8_t *, size_t, uint16_t, uint16_t,
    uint64_t timestamp_ms);
int df_runtime_media_module_push_video(struct df_runtime_media_module *,
    const struct df_gvs_video_packet *, uint32_t source_ipv4,
    uint64_t timestamp_ms);
int df_runtime_media_module_tick(struct df_runtime_media_module *,
    uint64_t now_ms);
int df_runtime_media_module_status(const struct df_runtime_media_module *,
    struct df_media_module_status_v3 *);
void df_runtime_media_module_stop(struct df_runtime_media_module *);

#endif
