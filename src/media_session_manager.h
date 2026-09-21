#ifndef DOORFAST_MEDIA_SESSION_MANAGER_H
#define DOORFAST_MEDIA_SESSION_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "media_session.h"

typedef int (*df_media_session_resource_start_fn)(
    struct df_media_session *, uint64_t proposed_generation, void *context);
typedef void (*df_media_session_resource_stop_fn)(
    struct df_media_session *, void *context);

struct df_media_session_resource_hooks {
    df_media_session_resource_start_fn start;
    df_media_session_resource_stop_fn stop;
    void *context;
};

enum df_media_video_reject_reason {
    DF_MEDIA_VIDEO_REJECT_DESTINATION = 0,
    DF_MEDIA_VIDEO_REJECT_SOURCE_IPV4,
    DF_MEDIA_VIDEO_REJECT_SESSION,
    DF_MEDIA_VIDEO_REJECT_MONITOR,
    DF_MEDIA_VIDEO_REJECT_JPEG,
    DF_MEDIA_VIDEO_REJECT_DIMENSIONS,
    DF_MEDIA_VIDEO_REJECT_ENCODER,
};

const char *df_media_video_reject_reason_name(
    enum df_media_video_reject_reason);

struct df_media_session_manager {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct df_media_station_config_v3 *stations;
    struct df_media_session *sessions;
    size_t capacity;
    size_t active_count;
    uint64_t next_generation;
    struct df_media_session_resource_hooks resource_hooks;
    struct df_media_credentials credentials;
    char *go2rtc_host;
    char *rtsp_username;
    char *credentials_path;
    char preempted_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t preempted_generation;
    char failed_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t failed_generation;
    enum df_media_error_v3 failed_error;
    uint64_t status_fingerprint;
    uint64_t status_revision;
    bool status_initialized;
    bool initialized;
};

/*
 * Storage must be zero-initialized before its first init. An initialized
 * manager rejects another init; destroy it before initializing it again.
 */
int df_media_session_manager_init(struct df_media_session_manager *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
void df_media_session_manager_destroy(struct df_media_session_manager *);
int df_media_session_manager_start(struct df_media_session_manager *,
    const char *station_id, enum df_media_session_purpose, uint64_t now_ms,
    uint64_t *generation);
int df_media_session_manager_command(struct df_media_session_manager *,
    enum df_media_module_command, const struct df_media_session_key *,
    bool active, uint64_t now_ms);
int df_media_session_manager_receive_control(struct df_media_session_manager *,
    const struct df_gvs_frame *, uint32_t source_ipv4, uint64_t now_ms);
int df_media_session_manager_incoming_call(struct df_media_session_manager *,
    const char *station_id, uint64_t call_generation, uint64_t now_ms);
const struct df_media_session *df_media_session_manager_find(
    const struct df_media_session_manager *, const char *station_id);
size_t df_media_session_manager_active(
    const struct df_media_session_manager *);
size_t df_media_session_manager_capacity(
    const struct df_media_session_manager *);
const struct df_media_session *df_media_session_manager_lookup(
    const struct df_media_session_manager *,
    const struct df_media_session_key *);
void df_media_session_manager_set_resource_hooks(
    struct df_media_session_manager *,
    const struct df_media_session_resource_hooks *);
int df_media_session_manager_push_jpeg(struct df_media_session_manager *,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    const uint8_t *jpeg, size_t length, uint16_t width, uint16_t height,
    uint64_t timestamp_ms);
int df_media_session_manager_push_video(struct df_media_session_manager *,
    const struct df_gvs_video_packet *, uint32_t source_ipv4,
    uint64_t timestamp_ms);
int df_media_session_manager_tick(struct df_media_session_manager *,
    uint64_t now_ms);

#endif
