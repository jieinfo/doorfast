#ifndef DOORFAST_RUNTIME_MEDIA_MODULE_H
#define DOORFAST_RUNTIME_MEDIA_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "media_module.h"

#define DF_RUNTIME_MEDIA_MODULE_PATH "/usr/lib/doorfast/media-v3.so"

struct df_runtime_incoming_media_binding {
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint8_t peer[6];
    uint64_t control_generation, media_generation, retry_ms;
    bool cleanup_pending;
    int cleanup_error;
};

struct df_runtime_media_module {
    void *handle;
    const struct df_media_module_api_v3 *api;
    const struct df_media_module_outbound_upgrade_api_v1 *outbound_upgrade_api;
    const struct df_media_module_lifecycle_api_v2 *lifecycle_api;
    struct df_media_module_callbacks_v3 original_callbacks, adapted_callbacks;
    struct df_runtime_incoming_media_binding *incoming_bindings;
    size_t incoming_binding_count;
    uint8_t local[6];
    const struct df_runtime_incoming_media_binding *suppress_stop;
    const struct df_media_module_diagnostics_api_v1 *diagnostics_api;
    const struct df_media_module_diagnostics_api_v2 *diagnostics_api_v2;
    void *instance;
    struct df_media_session_status_v3 *session_snapshot;
    size_t session_snapshot_capacity;
    char reported_failed_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t reported_failed_generation;
    bool available;
};

int df_runtime_media_module_validate_api_v3(
    const struct df_media_module_api_v3 *);
int df_runtime_media_module_set_outbound_api(struct df_runtime_media_module *,
    const struct df_media_module_outbound_upgrade_api_v1 *);
int df_runtime_media_module_set_lifecycle_api(struct df_runtime_media_module *,
    const struct df_media_module_lifecycle_api_v2 *);
int df_runtime_media_module_release_incoming(struct df_runtime_media_module *,
    const char *, uint64_t control_generation, enum df_media_outbound_operation,
    uint64_t now_ms);
int df_runtime_media_module_outbound_operation(struct df_runtime_media_module *,
    enum df_media_outbound_operation, const struct df_media_session_key *,
    uint64_t control_generation, uint64_t now_ms);
int df_runtime_media_module_call_key(struct df_runtime_media_module *,
    const struct df_media_session_key *, uint64_t now_ms);

int df_runtime_media_module_start(struct df_runtime_media_module *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
int df_runtime_media_module_start_with_api(struct df_runtime_media_module *,
    const struct df_media_module_api_v3 *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
int df_runtime_media_module_request_start(struct df_runtime_media_module *,
    const char *station_id, uint64_t now_ms, uint64_t *generation);
int df_runtime_media_module_call(struct df_runtime_media_module *,
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
int df_runtime_media_module_diagnostics(
    const struct df_runtime_media_module *,
    struct df_media_session_diagnostics_v1 *, size_t, size_t *);
int df_runtime_media_module_diagnostics_v2(
    const struct df_runtime_media_module *, uint64_t now_ms,
    struct df_media_module_diagnostics_v2 *);
const char *df_media_timer_name(enum df_media_timer);
const char *df_media_end_reason_name(enum df_media_end_reason);
const char *df_media_failure_reason_name(enum df_media_failure_reason);
void df_runtime_media_module_stop(struct df_runtime_media_module *);

#endif
