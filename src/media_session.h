#ifndef DOORFAST_MEDIA_SESSION_H
#define DOORFAST_MEDIA_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "media_module.h"
#include "gvs_video_reassembly.h"

struct df_media_session {
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    char stream_name[DF_MEDIA_MODULE_STREAM_MAX];
    uint8_t station[6];
    uint32_t station_ipv4;
    uint64_t generation;
    uint64_t call_generation;
    enum df_media_session_purpose purpose;
    enum df_media_session_state_v3 state;
    enum df_media_error_v3 last_error;
    bool reserved;
    bool active;
    bool viewer_active;
    bool queue_initialized;
    uint64_t started_ms;
    uint64_t frames_received;
    uint64_t last_frame_ms;
    uint64_t status_fingerprint;
    uint64_t status_revision;
    bool status_initialized;
    struct df_gvs_video_reassembly video;
    struct df_gvs_monitor monitor;
    struct df_media_frame_queue queue;
    struct df_media_encoder_process encoder;
    void (*stop_resources)(struct df_media_session *, void *context);
    void *resource_context;
};

void df_media_session_reset(struct df_media_session *);
int df_media_session_prepare(struct df_media_session *,
    const struct df_media_station_config_v3 *, uint32_t station_ipv4,
    enum df_media_session_purpose, uint64_t now_ms);
int df_media_session_activate(struct df_media_session *, uint64_t generation);
int df_media_session_publish(struct df_media_session *,
    const struct df_media_station_config_v3 *, uint32_t station_ipv4,
    enum df_media_session_purpose, uint64_t generation, uint64_t now_ms);
bool df_media_session_matches_station(const struct df_media_session *,
    const char *station_id);
bool df_media_session_matches_key(const struct df_media_session *,
    const struct df_media_session_key *);
int df_media_session_command(struct df_media_session *,
    enum df_media_module_command, const struct df_media_session_key *,
    bool active, uint64_t now_ms);
int df_media_session_push_jpeg(struct df_media_session *,
    const struct df_media_module_config_v3 *,
    const struct df_media_credentials *, const uint8_t *, size_t,
    uint16_t width, uint16_t height, uint64_t timestamp_ms);
int df_media_session_tick_pipeline(struct df_media_session *, uint64_t now_ms);
int df_media_session_stop_pipeline(struct df_media_session *);
int df_media_session_upgrade_to_call(struct df_media_session *,
    uint64_t generation, uint64_t call_generation, uint64_t now_ms);

#endif
