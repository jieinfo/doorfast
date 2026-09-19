#ifndef DOORFAST_RUNTIME_UBUS_H
#define DOORFAST_RUNTIME_UBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "runtime_id.h"
#include "gvs_access.h"
#include "gvs_elevator_control.h"
#include "gvs_runtime_sync.h"
#include "gvs_call_control.h"
#include "gvs_audio_buffer.h"
#include "gvs_audio_tx.h"
#include "gvs_station_discovery.h"
#include "gvs_video_frame_cache.h"
#include "media_credentials.h"
#include "runtime_media_module.h"
#include "station_registry.h"

#define DF_RUNTIME_UBUS_LOG_CAPACITY 128U
#define DF_RUNTIME_UBUS_LOG_MESSAGE_MAX 160U
#define DF_RUNTIME_UBUS_MEDIA_PATH_MAX 256U
#define DF_RUNTIME_MEDIA_ERROR_RUNTIME_MISMATCH 200
#define DF_RUNTIME_STATION_ADDRESS_TEXT_SIZE 18U
#define DF_RUNTIME_STATION_IPV4_TEXT_SIZE 16U
#define DF_RUNTIME_STATION_ROUTE_SOURCE_SIZE 11U

struct df_runtime_log_entry {
    uint64_t sequence;
    uint64_t timestamp_ms;
    char message[DF_RUNTIME_UBUS_LOG_MESSAGE_MAX];
};

typedef int (*df_runtime_status_provider_fn)(
    struct df_gvs_runtime_sync_status *status, void *context);
typedef int (*df_runtime_call_status_provider_fn)(
    struct df_gvs_call_control_status *status, void *context);

struct df_runtime_call_request {
    enum df_gvs_call_command_type type;
    char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
    uint64_t session_generation;
    uint16_t primary_media_port;
    uint16_t secondary_media_port;
    uint8_t duration_seconds;
    uint8_t reason;
};

typedef int (*df_runtime_call_submit_fn)(
    const struct df_runtime_call_request *, uint64_t, void *);

struct df_runtime_elevator_status {
    enum df_gvs_elevator_control_state state;
    enum df_gvs_elevator_direction direction;
    uint64_t transaction_id;
    unsigned attempts;
    unsigned successful_sends;
    bool physical_result_confirmed;
    bool status_valid;
    size_t count;
    uint64_t age_ms;
    struct df_gvs_elevator_entry entries[DF_GVS_ELEVATOR_MAX_ENTRIES];
};

struct df_runtime_media_status {
    bool installed;
    bool available;
    size_t configured_capacity;
    size_t effective_capacity;
    size_t active_encoders;
    uint64_t status_revision;
    char preempted_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t preempted_generation;
    const struct df_media_session_status_v3 *sessions;
    size_t session_count;
    bool rtsp_password_set;
    bool has_credential_text;
};

struct df_station_snapshot_entry {
    char id[33];
    char name[65];
    char logical_address[DF_RUNTIME_STATION_ADDRESS_TEXT_SIZE];
    bool enabled;
    char stream_name[65];
    char route_source[DF_RUNTIME_STATION_ROUTE_SOURCE_SIZE];
    bool route_fresh;
    bool monitorable;
    bool has_last_seen;
    uint64_t last_seen_ms;
};

struct df_station_snapshot {
    char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
    uint64_t revision;
    const struct df_station_snapshot_entry *stations;
    size_t count;
};

struct df_station_candidate_snapshot_entry {
    char logical_address[DF_RUNTIME_STATION_ADDRESS_TEXT_SIZE];
    char ipv4[DF_RUNTIME_STATION_IPV4_TEXT_SIZE];
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint64_t reply_count;
    bool configured;
};

struct df_station_candidate_snapshot {
    char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
    const struct df_station_candidate_snapshot_entry *candidates;
    size_t count;
};

struct df_runtime_station_route_entry;

struct df_runtime_ubus {
    df_runtime_status_provider_fn provide_status;
    void *status_context;
    df_runtime_call_status_provider_fn provide_call_status;
    df_runtime_call_submit_fn submit_call;
    void *call_context;
    struct df_gvs_access_control *access;
    const struct df_gvs_session *access_session;
    const uint8_t *access_identity;
    struct df_gvs_elevator_control *elevator;
    struct df_gvs_audio_buffer *audio;
    struct df_gvs_audio_tx *audio_tx;
    struct df_gvs_video_frame_cache *video;
    struct df_runtime_media_module *media;
    struct df_media_session_status_v3 *media_session_entries;
    size_t media_session_capacity;
    char media_credentials_path[DF_RUNTIME_UBUS_MEDIA_PATH_MAX];
    const uint8_t *elevator_identity;
    uint64_t next_elevator_transaction_id;
    struct df_gvs_elevator_status observed_elevator_status;
    uint64_t elevator_status_observed_ms;
    bool elevator_status_valid;
    void *platform;
    uint64_t last_now_ms;
    uint64_t next_reconnect_ms;
    char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
    bool started;
    bool active_host;
    struct df_runtime_log_entry log_entries[DF_RUNTIME_UBUS_LOG_CAPACITY];
    size_t log_count;
    size_t log_next;
    uint64_t log_sequence;
    const struct df_station_registry *station_registry;
    const struct df_gvs_station_discovery *station_discovery;
    struct df_runtime_station_route_entry *station_route_entries;
    struct df_gvs_station_scan *station_scan;
    struct df_station_snapshot_entry *station_snapshot_entries;
    struct df_station_candidate_snapshot_entry station_candidate_entries[
        DF_GVS_STATION_CANDIDATE_CAPACITY];
    uint8_t station_identity[6];
    bool stations_bound;
    bool station_scan_enabled;
};

int df_runtime_ubus_start(struct df_runtime_ubus *service,
                          df_runtime_status_provider_fn provide_status,
                          void *context, uint64_t now_ms);
int df_runtime_ubus_process(struct df_runtime_ubus *service,
                            uint64_t now_ms);
int df_runtime_ubus_bind_call(struct df_runtime_ubus *,
    df_runtime_call_status_provider_fn, df_runtime_call_submit_fn, void *);
void df_runtime_ubus_set_active_host(struct df_runtime_ubus *, bool);
const char *df_runtime_ubus_handshake_mode(const struct df_runtime_ubus *);
int df_runtime_ubus_read_call_status(struct df_runtime_ubus *,
    struct df_gvs_call_control_status *);
int df_runtime_ubus_submit_call(struct df_runtime_ubus *,
    const struct df_runtime_call_request *);
void df_runtime_ubus_stop(struct df_runtime_ubus *service);

int df_runtime_ubus_bind_access(struct df_runtime_ubus *,
    struct df_gvs_access_control *, const struct df_gvs_session *,
    const uint8_t [6]);
int df_runtime_ubus_unlock(struct df_runtime_ubus *, const char *runtime_id,
    uint64_t);
int df_runtime_ubus_bind_elevator(struct df_runtime_ubus *,
    struct df_gvs_elevator_control *, const uint8_t [6]);
int df_runtime_ubus_call_elevator(struct df_runtime_ubus *,
    const char *runtime_id, enum df_gvs_elevator_direction, uint64_t *);
int df_runtime_ubus_update_elevator_status(struct df_runtime_ubus *,
    const struct df_gvs_elevator_status *, uint64_t);
int df_runtime_ubus_read_elevator_status(struct df_runtime_ubus *,
    struct df_runtime_elevator_status *);
int df_runtime_ubus_bind_audio(struct df_runtime_ubus *, struct df_gvs_audio_buffer *);
int df_runtime_ubus_read_audio_status(struct df_runtime_ubus *, struct df_gvs_audio_status *);
int df_runtime_ubus_bind_audio_tx(struct df_runtime_ubus *,
    struct df_gvs_audio_tx *);
int df_runtime_ubus_read_audio_tx_status(struct df_runtime_ubus *,
    struct df_gvs_audio_tx_status *);
int df_runtime_ubus_bind_video(struct df_runtime_ubus *,
    struct df_gvs_video_frame_cache *);
int df_runtime_ubus_read_video_status(struct df_runtime_ubus *,
    struct df_gvs_video_status *);
int df_runtime_ubus_bind_media(struct df_runtime_ubus *,
    struct df_runtime_media_module *, const char *credentials_path);
int df_runtime_ubus_monitor_start(struct df_runtime_ubus *,
    const char *runtime_id, const char *station_id, uint64_t *generation);
int df_runtime_ubus_monitor_stop(struct df_runtime_ubus *,
    const char *runtime_id, const char *station_id, uint64_t generation);
int df_runtime_ubus_monitor_viewer(struct df_runtime_ubus *,
    const char *runtime_id, const char *station_id, uint64_t generation,
    bool active);
int df_runtime_ubus_read_media_status(struct df_runtime_ubus *,
    struct df_runtime_media_status *);
int df_runtime_ubus_update_media_credentials(struct df_runtime_ubus *,
    const struct df_media_credentials_update *);
int df_runtime_ubus_log_event(struct df_runtime_ubus *, uint64_t,
    const char *);
size_t df_runtime_ubus_log_count(const struct df_runtime_ubus *);
int df_runtime_ubus_log_get(const struct df_runtime_ubus *, size_t,
    struct df_runtime_log_entry *);
int df_runtime_ubus_bind_stations(struct df_runtime_ubus *,
    const struct df_station_registry *,
    const struct df_gvs_station_discovery *,
    struct df_gvs_station_scan *,
    const uint8_t identity[6], bool scan_enabled);
int df_runtime_ubus_station_route_observe(struct df_runtime_ubus *,
    const uint8_t logical_address[6], uint32_t ipv4, uint64_t now_ms,
    bool discovery_reply);
int df_runtime_ubus_station_list(struct df_runtime_ubus *,
    struct df_station_snapshot *);
int df_runtime_ubus_station_candidates(struct df_runtime_ubus *,
    struct df_station_candidate_snapshot *);
int df_runtime_ubus_station_scan(struct df_runtime_ubus *, uint64_t now_ms);

#endif
