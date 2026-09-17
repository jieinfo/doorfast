#ifndef DOORFAST_CONFIG_H
#define DOORFAST_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_elevator.h"

#define DF_MEDIA_CREDENTIALS_PATH "/etc/doorfast/media-credentials"

enum df_media_encoder {
    DF_MEDIA_ENCODER_AUTO = 0,
    DF_MEDIA_ENCODER_SOFTWARE,
    DF_MEDIA_ENCODER_VAAPI,
    DF_MEDIA_ENCODER_QSV,
};

enum df_media_resolution {
    DF_MEDIA_RESOLUTION_SOURCE = 0,
    DF_MEDIA_RESOLUTION_480X640,
    DF_MEDIA_RESOLUTION_360X480,
    DF_MEDIA_RESOLUTION_240X320,
};

enum df_media_profile {
    DF_MEDIA_PROFILE_BASELINE = 0,
    DF_MEDIA_PROFILE_MAIN,
};

enum df_media_overload_policy {
    DF_MEDIA_OVERLOAD_REJECT_NEW = 0,
    DF_MEDIA_OVERLOAD_STOP_OLDEST_PREVIEW,
};

struct df_media_config {
    bool enabled;
    const char *station_address;
    const char *station_ipv4;
    const char *go2rtc_host;
    const char *stream_name;
    const char *rtsp_username;
    const char *credentials_path;
    const char *relay_url;
    uint16_t go2rtc_port;
    enum df_media_encoder encoder;
    enum df_media_resolution resolution;
    uint8_t fps;
    uint16_t bitrate_kbps;
    enum df_media_profile profile;
    uint8_t max_encoders;
    uint32_t min_free_kib;
    uint16_t preview_timeout_s;
    uint8_t first_frame_timeout_s;
    uint8_t publish_retries;
    enum df_media_overload_policy overload_policy;
    bool diagnostics;
};

struct df_config {
    bool enabled;
    const char *brand;
    const char *capture_interface;
    bool capture_auto;
    bool capture_promiscuous;
    const char *gvs_interface;
    const char *gvs_local_address;
    const char *indoor_ipaddr;
    const char *indoor_netmask;
    const char *uplink_interface;
    const char *sync_state_path;
    const char *access_material;
    bool passive_only;
    bool active_host;
    int unlock_delay_seconds;
    int hangup_delay_seconds;
    bool call_elev;
    struct df_media_config media;
};

#define DF_LEGACY_BRAND_MAX 32

struct df_legacy_import {
    struct df_config config;
    char brand[DF_LEGACY_BRAND_MAX];
};

int df_config_validate(const struct df_config *config);
void df_config_redact(char *dst, size_t dst_size, const char *secret);
int df_config_import_legacy(const char *legacy_uci, struct df_legacy_import *imported);

#endif
