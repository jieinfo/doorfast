#ifndef DOORFAST_MEDIA_MODULE_H
#define DOORFAST_MEDIA_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "gvs_frame.h"
#include "gvs_monitor.h"
#include "media_credentials.h"
#include "media_encoder.h"
#include "media_frame_queue.h"

#define DF_MEDIA_MODULE_ABI_VERSION 3U
#define DF_MEDIA_MODULE_ABI_VERSION_V2 2U
#define DF_MEDIA_MODULE_STATE_MAX 24U
#define DF_MEDIA_MODULE_FAILURE_MAX 96U
#define DF_MEDIA_MODULE_HOST_MAX 64U
#define DF_MEDIA_MODULE_STREAM_MAX 65U
#define DF_MEDIA_MODULE_USERNAME_MAX 33U
#define DF_MEDIA_MODULE_STATION_ID_MAX 33U

enum df_media_module_command {
    DF_MEDIA_MODULE_COMMAND_STOP = 1,
    DF_MEDIA_MODULE_COMMAND_VIEWER,
};

enum df_media_session_purpose {
    DF_MEDIA_SESSION_PREVIEW = 0,
    DF_MEDIA_SESSION_CALL,
};

enum df_media_call_policy {
    DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW = 0,
    DF_MEDIA_CALL_PRESERVE_PREVIEWS,
};

struct df_media_station_config_v3 {
    const char *id;
    const char *stream_name;
    bool enabled;
    uint8_t logical_address[6];
    uint32_t ipv4;
};

struct df_media_module_config_v3 {
    bool enabled;
    uint8_t local[6];
    const struct df_media_station_config_v3 *stations;
    size_t station_count;
    size_t max_encoders;
    enum df_media_call_policy incoming_call_policy;
    const char *go2rtc_host;
    uint16_t go2rtc_port;
    const char *rtsp_username;
    const char *credentials_path;
    enum df_media_encoder encoder;
    enum df_media_resolution resolution;
    uint8_t fps;
    uint16_t bitrate_kbps;
    enum df_media_profile profile;
    uint32_t min_free_kib;
    uint16_t preview_timeout_s;
    uint8_t first_frame_timeout_s;
};

struct df_media_session_key {
    const char *station_id;
    uint64_t generation;
};

struct df_media_session_status_v3 {
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t generation;
    enum df_media_session_purpose purpose;
    bool active;
};

struct df_media_module_status_v3 {
    /*
     * On input, session_count is the capacity of the caller-owned sessions
     * array. On output, required_session_count is the total snapshot size and
     * session_count is the number copied. Passing sessions == NULL queries the
     * required count. No pointer in a returned session is module-owned.
     */
    size_t required_session_count;
    size_t session_count;
    struct df_media_session_status_v3 *sessions;
};

struct df_media_module_config_v2 {
    bool enabled;
    uint8_t local[6];
    uint8_t station[6];
    uint32_t station_ipv4;
    const char *go2rtc_host;
    uint16_t go2rtc_port;
    const char *stream_name;
    const char *rtsp_username;
    const char *credentials_path;
    enum df_media_encoder encoder;
    enum df_media_resolution resolution;
    uint8_t fps;
    uint16_t bitrate_kbps;
    enum df_media_profile profile;
    uint32_t min_free_kib;
    uint16_t preview_timeout_s;
    uint8_t first_frame_timeout_s;
    const char *deprecated_relay_url;
};

typedef int (*df_media_module_emit_control_fn)(
    const uint8_t destination[6], uint32_t destination_ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t payload_length, void *context);
typedef int (*df_media_module_resolve_route_fn)(const uint8_t peer[6],
    uint64_t now_ms, uint32_t *ipv4, void *context);
typedef int (*df_media_module_encoder_stop_fn)(
    struct df_media_encoder_process *, unsigned timeout_ms);
typedef int (*df_media_module_encoder_start_fn)(
    struct df_media_encoder_process *, const struct df_media_encoder_config *,
    const struct df_media_credentials *, uint64_t generation);
typedef int (*df_media_module_available_memory_fn)(uint64_t *available_kib,
    void *context);

struct df_media_module_callbacks_v3 {
    df_media_module_emit_control_fn emit_control;
    df_media_module_resolve_route_fn resolve_route;
    df_media_module_available_memory_fn available_memory;
    void *context;
};

/*
 * create() copies station IDs, stream names, and scalar configuration. The
 * callback table and context must remain valid until destroy() returns.
 * start() and command() borrow their string/key arguments for the call only.
 */

struct df_media_module_api_v3 {
    uint32_t abi_version;
    size_t struct_size;
    void *(*create)(const struct df_media_module_config_v3 *,
                    const struct df_media_module_callbacks_v3 *);
    void (*destroy)(void *);
    int (*start)(void *, const char *, enum df_media_session_purpose,
                 uint64_t, uint64_t);
    int (*command)(void *, enum df_media_module_command,
                   const struct df_media_session_key *, bool, uint64_t);
    int (*receive_control)(void *, const struct df_gvs_frame *,
                           uint32_t, uint64_t);
    int (*push_jpeg)(void *, const uint8_t[6], const uint8_t[6], uint32_t,
                     const uint8_t *, size_t, uint16_t, uint16_t, uint64_t);
    int (*tick)(void *, uint64_t);
    int (*status)(const void *, struct df_media_module_status_v3 *);
};

extern const struct df_media_module_api_v3 df_media_module_api_v3;

int df_media_module_config_v3_validate(
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);

struct df_media_module_callbacks_v2 {
    df_media_module_emit_control_fn emit_control;
    df_media_module_resolve_route_fn resolve_route;
    int (*deprecated_relay_send)(const char *, const char *, const char *, void *);
    df_media_module_available_memory_fn available_memory;
    void *context;
};

struct df_media_module_status {
    bool available;
    bool encoder_running;
    enum df_gvs_monitor_state monitor_state;
    char state[DF_MEDIA_MODULE_STATE_MAX];
    char failure[DF_MEDIA_MODULE_FAILURE_MAX];
    uint64_t generation;
    uint64_t status_revision;
    unsigned queue_drops;
    unsigned deprecated_relay_failures;
};

struct df_media_module {
    struct df_media_module_config_v2 config;
    struct df_media_module_callbacks_v2 callbacks;
    struct df_gvs_monitor monitor;
    struct df_media_frame_queue queue;
    struct df_media_encoder_process encoder;
    struct df_media_credentials credentials;
    char go2rtc_host[DF_MEDIA_MODULE_HOST_MAX];
    char stream_name[DF_MEDIA_MODULE_STREAM_MAX];
    char rtsp_username[DF_MEDIA_MODULE_USERNAME_MAX];
    char credentials_path[256];
    df_media_module_encoder_start_fn start_encoder;
    df_media_module_encoder_stop_fn stop_encoder;
    bool queue_initialized;
    bool initialized;
    uint64_t status_revision;
    enum df_gvs_monitor_state revision_monitor_state;
    uint64_t revision_generation;
    unsigned revision_queue_drops;
    bool revision_encoder_running;
    char revision_failure[DF_MEDIA_MODULE_FAILURE_MAX];
    uint64_t preview_deadline_ms;
    char failure[DF_MEDIA_MODULE_FAILURE_MAX];
};

int df_media_module_init(struct df_media_module *,
    const struct df_media_module_config_v2 *,
    const struct df_media_module_callbacks_v2 *, uint64_t now_ms);
int df_media_module_start(struct df_media_module *, uint64_t now_ms);
int df_media_module_command(struct df_media_module *,
    enum df_media_module_command, uint64_t generation, bool active,
    uint64_t now_ms);
int df_media_module_receive_control(struct df_media_module *,
    const struct df_gvs_frame *, uint32_t source_ipv4, uint64_t now_ms);
int df_media_module_push_jpeg(struct df_media_module *,
    const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4,
    uint64_t generation, const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, uint64_t timestamp_ms);
int df_media_module_preempt(struct df_media_module *, uint64_t now_ms);
int df_media_module_tick(struct df_media_module *, uint64_t now_ms);
int df_media_module_status(const struct df_media_module *,
    struct df_media_module_status *);
int df_media_module_destroy(struct df_media_module *);

struct df_media_module_api_v2 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *(*create)(const struct df_media_module_config_v2 *,
                    const struct df_media_module_callbacks_v2 *);
    void (*destroy)(void *);
    int (*start)(void *, uint64_t);
    int (*command)(void *, enum df_media_module_command, uint64_t, bool,
                   uint64_t);
    int (*receive_control)(void *, const struct df_gvs_frame *, uint32_t,
                           uint64_t);
    int (*push_jpeg)(void *, const uint8_t[6], const uint8_t[6], uint32_t,
                     uint64_t, const uint8_t *, size_t, uint16_t, uint16_t,
                     uint64_t);
    int (*preempt)(void *, uint64_t);
    int (*tick)(void *, uint64_t);
    int (*status)(const void *, struct df_media_module_status *);
};

extern const struct df_media_module_api_v2 df_media_module_api_v2;

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(struct df_media_module_config_v2) == 96U,
    "media ABI v2 config size changed");
_Static_assert(offsetof(struct df_media_module_config_v2,
    deprecated_relay_url) == 88U, "media ABI v2 config relay slot moved");
_Static_assert(sizeof(struct df_media_module_callbacks_v2) == 40U,
    "media ABI v2 callbacks size changed");
_Static_assert(offsetof(struct df_media_module_callbacks_v2,
    deprecated_relay_send) == 16U, "media ABI v2 callback relay slot moved");
_Static_assert(sizeof(struct df_media_module_status) == 152U,
    "media ABI v2 status size changed");
_Static_assert(offsetof(struct df_media_module_status,
    deprecated_relay_failures) == 148U, "media ABI v2 status relay slot moved");
_Static_assert(sizeof(struct df_media_module_api_v3) == 80U,
    "media ABI v3 API size changed");
#endif
_Static_assert(_Generic(((struct df_media_module_config_v2 *)0)->
    deprecated_relay_url, const char *: 1, default: 0),
    "media ABI v2 config relay slot type changed");

#endif
