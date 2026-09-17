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
#include "media_relay.h"

#define DF_MEDIA_MODULE_ABI_VERSION 1U
#define DF_MEDIA_MODULE_STATE_MAX 24U
#define DF_MEDIA_MODULE_FAILURE_MAX 96U
#define DF_MEDIA_MODULE_HOST_MAX 64U
#define DF_MEDIA_MODULE_STREAM_MAX 65U
#define DF_MEDIA_MODULE_USERNAME_MAX 33U

enum df_media_module_command {
    DF_MEDIA_MODULE_COMMAND_STOP = 1,
    DF_MEDIA_MODULE_COMMAND_VIEWER,
};

struct df_media_module_config_v1 {
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
    const char *relay_url;
};

typedef int (*df_media_module_emit_control_fn)(
    const uint8_t destination[6], uint32_t destination_ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t payload_length, void *context);
typedef int (*df_media_module_resolve_route_fn)(const uint8_t peer[6],
    uint64_t now_ms, uint32_t *ipv4, void *context);
typedef int (*df_media_module_encoder_stop_fn)(
    struct df_media_encoder_process *, unsigned timeout_ms);

struct df_media_module_callbacks_v1 {
    df_media_module_emit_control_fn emit_control;
    df_media_module_resolve_route_fn resolve_route;
    df_media_relay_send_fn relay_send;
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
    unsigned relay_failures;
};

struct df_media_module {
    struct df_media_module_config_v1 config;
    struct df_media_module_callbacks_v1 callbacks;
    struct df_gvs_monitor monitor;
    struct df_media_frame_queue queue;
    struct df_media_encoder_process encoder;
    struct df_media_relay relay;
    struct df_media_credentials credentials;
    char go2rtc_host[DF_MEDIA_MODULE_HOST_MAX];
    char stream_name[DF_MEDIA_MODULE_STREAM_MAX];
    char rtsp_username[DF_MEDIA_MODULE_USERNAME_MAX];
    char credentials_path[256];
    char relay_url[DF_MEDIA_RELAY_URL_MAX];
    df_media_module_encoder_stop_fn stop_encoder;
    bool queue_initialized;
    bool initialized;
    uint64_t status_revision;
    char failure[DF_MEDIA_MODULE_FAILURE_MAX];
};

int df_media_module_init(struct df_media_module *,
    const struct df_media_module_config_v1 *,
    const struct df_media_module_callbacks_v1 *, uint64_t now_ms);
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

struct df_media_module_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *(*create)(const struct df_media_module_config_v1 *,
                    const struct df_media_module_callbacks_v1 *);
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

extern const struct df_media_module_api_v1 df_media_module_api_v1;

#endif
