#include "runtime_config.h"
#include "gvs_identity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DF_RUNTIME_CONFIG_MAX_BYTES 65536
#define DF_RUNTIME_LINE_MAX 512

#define DF_SEEN_ENABLED (1ULL << 0)
#define DF_SEEN_BRAND (1ULL << 1)
#define DF_SEEN_GVS_INTERFACE (1ULL << 2)
#define DF_SEEN_GVS_ADDRESS (1ULL << 3)
#define DF_SEEN_UPLINK_INTERFACE (1ULL << 4)
#define DF_SEEN_PASSIVE_ONLY (1ULL << 5)
#define DF_SEEN_PROMISCUOUS (1ULL << 6)
#define DF_SEEN_CAPTURE_AUTO (1ULL << 7)
#define DF_SEEN_UNLOCK_DELAY (1ULL << 8)
#define DF_SEEN_HANGUP_DELAY (1ULL << 9)
#define DF_SEEN_CALL_ELEV (1ULL << 10)
#define DF_SEEN_SYNC_STATE_PATH (1ULL << 11)
#define DF_SEEN_ACTIVE_HOST (1ULL << 12)
#define DF_SEEN_ACCESS_MATERIAL (1ULL << 13)
#define DF_SEEN_INDOOR_IPADDR (1ULL << 14)
#define DF_SEEN_INDOOR_NETMASK (1ULL << 15)
#define DF_SEEN_PASSIVE_INTERFACE (1ULL << 16)
#define DF_SEEN_HOST_INTERFACE (1ULL << 17)
#define DF_SEEN_MEDIA_ENABLED (1ULL << 18)
#define DF_SEEN_MEDIA_STATION_ADDRESS (1ULL << 19)
#define DF_SEEN_MEDIA_STATION_IPV4 (1ULL << 20)
#define DF_SEEN_MEDIA_GO2RTC_HOST (1ULL << 21)
#define DF_SEEN_MEDIA_GO2RTC_PORT (1ULL << 22)
#define DF_SEEN_MEDIA_STREAM_NAME (1ULL << 23)
#define DF_SEEN_MEDIA_RTSP_USERNAME (1ULL << 24)
#define DF_SEEN_MEDIA_ENCODER (1ULL << 25)
#define DF_SEEN_MEDIA_RESOLUTION (1ULL << 26)
#define DF_SEEN_MEDIA_FPS (1ULL << 27)
#define DF_SEEN_MEDIA_BITRATE (1ULL << 28)
#define DF_SEEN_MEDIA_PROFILE (1ULL << 29)
#define DF_SEEN_MEDIA_MAX_ENCODERS (1ULL << 30)
#define DF_SEEN_MEDIA_MIN_FREE_KIB (1ULL << 31)
#define DF_SEEN_MEDIA_PREVIEW_TIMEOUT (1ULL << 32)
#define DF_SEEN_MEDIA_FIRST_FRAME_TIMEOUT (1ULL << 33)
#define DF_SEEN_MEDIA_PUBLISH_RETRIES (1ULL << 34)
#define DF_SEEN_MEDIA_OVERLOAD_POLICY (1ULL << 35)
#define DF_SEEN_MEDIA_DIAGNOSTICS (1ULL << 36)
#define DF_SEEN_MEDIA_RELAY_URL (1ULL << 37)
#define DF_SEEN_SYNC_MINI1_SECRETKEY (1ULL << 38)
#define DF_SEEN_SYNC_MINI2_SECRETKEY (1ULL << 39)

static void df_runtime_config_defaults(struct df_runtime_config *runtime) {
    memset(runtime, 0, sizeof(*runtime));
    (void)snprintf(runtime->brand, sizeof(runtime->brand), "%s", "gvs");
    runtime->config.access_material = runtime->access_material;
    runtime->config.brand = runtime->brand;
    runtime->config.capture_interface = runtime->gvs_interface;
    runtime->config.gvs_interface = runtime->gvs_interface;
    runtime->config.gvs_local_address = runtime->gvs_local_address;
    runtime->config.indoor_ipaddr = runtime->indoor_ipaddr;
    runtime->config.indoor_netmask = runtime->indoor_netmask;
    runtime->config.uplink_interface = runtime->uplink_interface;
    runtime->config.sync_state_path = runtime->sync_state_path;
    runtime->config.sync_mini1_secretkey = runtime->sync_mini1_secretkey;
    runtime->config.sync_mini2_secretkey = runtime->sync_mini2_secretkey;
    runtime->config.media.station_address = runtime->media_station_address;
    runtime->config.media.station_ipv4 = runtime->media_station_ipv4;
    runtime->config.media.go2rtc_host = runtime->media_go2rtc_host;
    runtime->config.media.stream_name = runtime->media_stream_name;
    runtime->config.media.rtsp_username = runtime->media_rtsp_username;
    runtime->config.media.credentials_path = DF_MEDIA_CREDENTIALS_PATH;
    runtime->config.media.relay_url = runtime->media_relay_url;
    (void)snprintf(runtime->sync_state_path, sizeof(runtime->sync_state_path),
                   "%s", "/etc/config/doorfast-sync");
    runtime->config.passive_only = true;
    runtime->config.active_host = false;
    runtime->config.unlock_delay_seconds = -1;
    runtime->config.hangup_delay_seconds = -1;
    runtime->config.media.go2rtc_port = 8554U;
    runtime->config.media.encoder = DF_MEDIA_ENCODER_AUTO;
    runtime->config.media.resolution = DF_MEDIA_RESOLUTION_SOURCE;
    runtime->config.media.fps = 10U;
    runtime->config.media.bitrate_kbps = 800U;
    runtime->config.media.profile = DF_MEDIA_PROFILE_BASELINE;
    runtime->config.media.max_encoders = 0U;
    runtime->config.media.min_free_kib = 393216U;
    runtime->config.media.preview_timeout_s = 120U;
    runtime->config.media.first_frame_timeout_s = 8U;
    /* Kept only as a legacy parser field; production publication failures
     * are reported asynchronously and are not retried by this option. */
    runtime->config.media.publish_retries = 0U;
    runtime->config.media.overload_policy = DF_MEDIA_OVERLOAD_REJECT_NEW;
    runtime->config.media.diagnostics = true;
    (void)snprintf(runtime->media_stream_name, sizeof(runtime->media_stream_name),
                   "%s", "doorfast_preview");
    (void)snprintf(runtime->media_rtsp_username, sizeof(runtime->media_rtsp_username),
                   "%s", "doorfast");
}

static const char *df_skip_space(const char *cursor) {
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int df_parse_word(const char **cursor, char *output, size_t output_size) {
    const char *start = df_skip_space(*cursor);
    const char *end = start;
    size_t length;

    while (*end != '\0' && !isspace((unsigned char)*end)) {
        end++;
    }
    length = (size_t)(end - start);
    if (length == 0 || length >= output_size) {
        return DF_ERR_INVALID;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    *cursor = end;
    return DF_OK;
}

static int df_parse_quoted(const char **cursor, char *output, size_t output_size) {
    const char *start = df_skip_space(*cursor);
    const char *end;
    char quote;
    size_t length;

    if (*start != '\'' && *start != '"') {
        return DF_ERR_INVALID;
    }
    quote = *start++;
    end = strchr(start, quote);
    if (end == NULL) {
        return DF_ERR_INVALID;
    }
    length = (size_t)(end - start);
    if (length >= output_size) {
        return DF_ERR_INVALID;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    end = df_skip_space(end + 1);
    if (*end != '\0' && *end != '#') {
        return DF_ERR_INVALID;
    }
    *cursor = end;
    return DF_OK;
}

static int df_parse_directive(const char *line, const char *directive,
                              char *name, size_t name_size,
                              char *value, size_t value_size) {
    const char *cursor = df_skip_space(line);
    size_t directive_length = strlen(directive);

    if (strncmp(cursor, directive, directive_length) != 0 ||
        !isspace((unsigned char)cursor[directive_length])) {
        return DF_ERR_INVALID;
    }
    cursor += directive_length;
    if (df_parse_word(&cursor, name, name_size) != DF_OK ||
        df_parse_quoted(&cursor, value, value_size) != DF_OK) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

static int df_parse_boolean(const char *value, bool *output) {
    if (strcmp(value, "0") == 0) {
        *output = false;
        return DF_OK;
    }
    if (strcmp(value, "1") == 0) {
        *output = true;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

static int df_parse_delay(const char *value, int *output) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);

    if (end == value || *end != '\0' || parsed < -1 || parsed > 9) {
        return DF_ERR_INVALID;
    }
    *output = (int)parsed;
    return DF_OK;
}

static int df_copy_option(char *destination, size_t destination_size, const char *value) {
    size_t length = strlen(value);

    if (length >= destination_size) {
        return DF_ERR_INVALID;
    }
    memcpy(destination, value, length + 1);
    return DF_OK;
}

static int df_claim_option(uint64_t *seen, uint64_t option) {
    if ((*seen & option) != 0U) {
        return DF_ERR_INVALID;
    }
    *seen |= option;
    return DF_OK;
}

static int df_parse_unsigned_range(const char *value, unsigned long minimum,
                                   unsigned long maximum, unsigned long *output) {
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0' ||
        strspn(value, "0123456789") != strlen(value)) return DF_ERR_INVALID;
    parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed < minimum || parsed > maximum)
        return DF_ERR_INVALID;
    *output = parsed;
    return DF_OK;
}

static int df_parse_media_encoder(const char *value, enum df_media_encoder *output) {
    if (strcmp(value, "auto") == 0) *output = DF_MEDIA_ENCODER_AUTO;
    else if (strcmp(value, "software") == 0) *output = DF_MEDIA_ENCODER_SOFTWARE;
    else if (strcmp(value, "vaapi") == 0) *output = DF_MEDIA_ENCODER_VAAPI;
    else if (strcmp(value, "qsv") == 0) *output = DF_MEDIA_ENCODER_QSV;
    else return DF_ERR_INVALID;
    return DF_OK;
}

static int df_parse_media_resolution(const char *value,
                                     enum df_media_resolution *output) {
    if (strcmp(value, "source") == 0) *output = DF_MEDIA_RESOLUTION_SOURCE;
    else if (strcmp(value, "480x640") == 0) *output = DF_MEDIA_RESOLUTION_480X640;
    else if (strcmp(value, "360x480") == 0) *output = DF_MEDIA_RESOLUTION_360X480;
    else if (strcmp(value, "240x320") == 0) *output = DF_MEDIA_RESOLUTION_240X320;
    else return DF_ERR_INVALID;
    return DF_OK;
}

static int df_parse_media_profile(const char *value, enum df_media_profile *output) {
    if (strcmp(value, "baseline") == 0) *output = DF_MEDIA_PROFILE_BASELINE;
    else if (strcmp(value, "main") == 0) *output = DF_MEDIA_PROFILE_MAIN;
    else return DF_ERR_INVALID;
    return DF_OK;
}

static int df_parse_media_overload_policy(const char *value,
                                          enum df_media_overload_policy *output) {
    if (strcmp(value, "reject_new") == 0) *output = DF_MEDIA_OVERLOAD_REJECT_NEW;
    else if (strcmp(value, "stop_oldest_preview") == 0)
        *output = DF_MEDIA_OVERLOAD_STOP_OLDEST_PREVIEW;
    else return DF_ERR_INVALID;
    return DF_OK;
}

static bool df_runtime_media_option_is_forbidden(const char *name) {
    if (strcmp(name, "media_credentials_path") == 0) return true;
    return strncmp(name, "media_", sizeof("media_") - 1U) == 0 &&
           (strstr(name, "password") != NULL || strstr(name, "token") != NULL);
}

static int df_apply_option(struct df_runtime_config *runtime, const char *name,
                           const char *value, uint64_t *seen) {
    uint64_t option = 0;
    unsigned long parsed;
    int result = DF_OK;

    if (strcmp(name, "access_material") == 0) {
        if (df_claim_option(seen, DF_SEEN_ACCESS_MATERIAL) != DF_OK)
            return DF_ERR_INVALID;
        if (value[0] != '\0' && (strlen(value) != 16U ||
            strspn(value, "0123456789abcdefABCDEF") != 16U))
            return DF_ERR_INVALID;
        return df_copy_option(runtime->access_material,
                              sizeof(runtime->access_material), value);
    }
    if (strcmp(name, "enabled") == 0) {
        option = DF_SEEN_ENABLED;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.enabled);
    }
    if (strcmp(name, "brand") == 0) {
        option = DF_SEEN_BRAND;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->brand, sizeof(runtime->brand), value);
    }
    if (strcmp(name, "gvs_interface") == 0) {
        option = DF_SEEN_GVS_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->gvs_interface, sizeof(runtime->gvs_interface), value);
    }
    if (strcmp(name, "passive_interface") == 0) {
        option = DF_SEEN_PASSIVE_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->passive_interface,
                              sizeof(runtime->passive_interface), value);
    }
    if (strcmp(name, "host_interface") == 0) {
        option = DF_SEEN_HOST_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->host_interface,
                              sizeof(runtime->host_interface), value);
    }
    if (strcmp(name, "gvs_local_address") == 0) {
        option = DF_SEEN_GVS_ADDRESS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->gvs_local_address, sizeof(runtime->gvs_local_address), value);
    }
    if (strcmp(name, "indoor_ipaddr") == 0) {
        option = DF_SEEN_INDOOR_IPADDR;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->indoor_ipaddr,
                              sizeof(runtime->indoor_ipaddr), value);
    }
    if (strcmp(name, "indoor_netmask") == 0) {
        option = DF_SEEN_INDOOR_NETMASK;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->indoor_netmask,
                              sizeof(runtime->indoor_netmask), value);
    }
    if (strcmp(name, "uplink_interface") == 0) {
        option = DF_SEEN_UPLINK_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->uplink_interface, sizeof(runtime->uplink_interface), value);
    }
    if (strcmp(name, "sync_state_path") == 0) {
        option = DF_SEEN_SYNC_STATE_PATH;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->sync_state_path,
                              sizeof(runtime->sync_state_path), value);
    }
    if (strcmp(name, "sync_mini1_secretkey") == 0) {
        option = DF_SEEN_SYNC_MINI1_SECRETKEY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->sync_mini1_secretkey,
                              sizeof(runtime->sync_mini1_secretkey), value);
    }
    if (strcmp(name, "sync_mini2_secretkey") == 0) {
        option = DF_SEEN_SYNC_MINI2_SECRETKEY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->sync_mini2_secretkey,
                              sizeof(runtime->sync_mini2_secretkey), value);
    }
    if (strcmp(name, "passive_only") == 0) {
        option = DF_SEEN_PASSIVE_ONLY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.passive_only);
    }
    if (strcmp(name, "active_host") == 0) {
        option = DF_SEEN_ACTIVE_HOST;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.active_host);
    }
    if (strcmp(name, "capture_promiscuous") == 0) {
        option = DF_SEEN_PROMISCUOUS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.capture_promiscuous);
    }
    if (strcmp(name, "capture_auto") == 0) {
        option = DF_SEEN_CAPTURE_AUTO;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.capture_auto);
    }
    if (strcmp(name, "unlock") == 0 || strcmp(name, "unlock_delay_seconds") == 0) {
        option = DF_SEEN_UNLOCK_DELAY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_delay(value, &runtime->config.unlock_delay_seconds);
    }
    if (strcmp(name, "hangup") == 0 || strcmp(name, "hangup_delay_seconds") == 0) {
        option = DF_SEEN_HANGUP_DELAY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_delay(value, &runtime->config.hangup_delay_seconds);
    }
    if (strcmp(name, "call_elev") == 0) {
        option = DF_SEEN_CALL_ELEV;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.call_elev);
    }
    if (strcmp(name, "media_enabled") == 0) {
        option = DF_SEEN_MEDIA_ENABLED;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.media.enabled);
    }
    if (strcmp(name, "media_station_address") == 0) {
        option = DF_SEEN_MEDIA_STATION_ADDRESS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_station_address,
                              sizeof(runtime->media_station_address), value);
    }
    if (strcmp(name, "media_station_ipv4") == 0) {
        option = DF_SEEN_MEDIA_STATION_IPV4;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_station_ipv4,
                              sizeof(runtime->media_station_ipv4), value);
    }
    if (strcmp(name, "media_go2rtc_host") == 0) {
        option = DF_SEEN_MEDIA_GO2RTC_HOST;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_go2rtc_host,
                              sizeof(runtime->media_go2rtc_host), value);
    }
    if (strcmp(name, "media_go2rtc_port") == 0) {
        option = DF_SEEN_MEDIA_GO2RTC_PORT;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 1U, 65535U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.go2rtc_port = (uint16_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_stream_name") == 0) {
        option = DF_SEEN_MEDIA_STREAM_NAME;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_stream_name,
                              sizeof(runtime->media_stream_name), value);
    }
    if (strcmp(name, "media_rtsp_username") == 0) {
        option = DF_SEEN_MEDIA_RTSP_USERNAME;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_rtsp_username,
                              sizeof(runtime->media_rtsp_username), value);
    }
    if (strcmp(name, "media_encoder") == 0) {
        option = DF_SEEN_MEDIA_ENCODER;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_media_encoder(value, &runtime->config.media.encoder);
    }
    if (strcmp(name, "media_resolution") == 0) {
        option = DF_SEEN_MEDIA_RESOLUTION;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_media_resolution(value, &runtime->config.media.resolution);
    }
    if (strcmp(name, "media_fps") == 0) {
        option = DF_SEEN_MEDIA_FPS;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 5U, 15U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.fps = (uint8_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_bitrate_kbps") == 0) {
        option = DF_SEEN_MEDIA_BITRATE;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 256U, 2000U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.bitrate_kbps = (uint16_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_profile") == 0) {
        option = DF_SEEN_MEDIA_PROFILE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_media_profile(value, &runtime->config.media.profile);
    }
    if (strcmp(name, "media_max_encoders") == 0) {
        option = DF_SEEN_MEDIA_MAX_ENCODERS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        if (strcmp(value, "auto") == 0) {
            runtime->config.media.max_encoders = 0U;
            return DF_OK;
        }
        if (df_parse_unsigned_range(value, 1U, 4U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.max_encoders = (uint8_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_min_free_kib") == 0) {
        option = DF_SEEN_MEDIA_MIN_FREE_KIB;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 131072U, 1048576U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.min_free_kib = (uint32_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_preview_timeout") == 0) {
        option = DF_SEEN_MEDIA_PREVIEW_TIMEOUT;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 15U, 600U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.preview_timeout_s = (uint16_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_first_frame_timeout") == 0) {
        option = DF_SEEN_MEDIA_FIRST_FRAME_TIMEOUT;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 2U, 30U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.first_frame_timeout_s = (uint8_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_publish_retries") == 0) {
        option = DF_SEEN_MEDIA_PUBLISH_RETRIES;
        if (df_claim_option(seen, option) != DF_OK ||
            df_parse_unsigned_range(value, 0U, 5U, &parsed) != DF_OK)
            return DF_ERR_INVALID;
        runtime->config.media.publish_retries = (uint8_t)parsed;
        return DF_OK;
    }
    if (strcmp(name, "media_overload_policy") == 0) {
        option = DF_SEEN_MEDIA_OVERLOAD_POLICY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_media_overload_policy(value,
                                              &runtime->config.media.overload_policy);
    }
    if (strcmp(name, "media_diagnostics") == 0) {
        option = DF_SEEN_MEDIA_DIAGNOSTICS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.media.diagnostics);
    }
    if (strcmp(name, "media_relay_url") == 0) {
        option = DF_SEEN_MEDIA_RELAY_URL;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->media_relay_url,
                              sizeof(runtime->media_relay_url), value);
    }
    return result;
}

int df_runtime_config_parse(const char *uci_text, struct df_runtime_config *runtime) {
    const char *cursor;
    bool in_main = false;
    bool found_main = false;
    uint64_t seen = 0;

    if (uci_text == NULL || runtime == NULL) {
        return DF_ERR_INVALID;
    }
    df_runtime_config_defaults(runtime);
    cursor = uci_text;
    while (*cursor != '\0') {
        char line[DF_RUNTIME_LINE_MAX];
        char name[64];
        char value[DF_RUNTIME_MEDIA_RELAY_URL_MAX];
        const char *line_end = strchr(cursor, '\n');
        const char *trimmed;
        size_t line_length = line_end == NULL ? strlen(cursor) : (size_t)(line_end - cursor);

        if (line_length >= sizeof(line)) {
            return DF_ERR_INVALID;
        }
        memcpy(line, cursor, line_length);
        line[line_length] = '\0';
        cursor = line_end == NULL ? cursor + line_length : line_end + 1;
        trimmed = df_skip_space(line);
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        if (strncmp(trimmed, "config", 6) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (df_parse_directive(trimmed, "config", name, sizeof(name),
                                   value, sizeof(value)) != DF_OK) {
                return DF_ERR_INVALID;
            }
            in_main = strcmp(name, "gvs") == 0 && strcmp(value, "main") == 0;
            if (in_main) {
                if (found_main) return DF_ERR_INVALID;
                found_main = true;
            }
            continue;
        }
        if (strncmp(trimmed, "option", 6) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (df_parse_directive(trimmed, "option", name, sizeof(name),
                                   value, sizeof(value)) != DF_OK) {
                return DF_ERR_INVALID;
            }
            if (df_runtime_media_option_is_forbidden(name)) return DF_ERR_INVALID;
            if (in_main && df_apply_option(runtime, name, value, &seen) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
    }
    if (!found_main) {
        return DF_ERR_INVALID;
    }
    if (runtime->config.active_host) {
        if (runtime->host_interface[0] != '\0') {
            if (df_copy_option(runtime->gvs_interface,
                               sizeof(runtime->gvs_interface),
                               runtime->host_interface) != DF_OK)
                return DF_ERR_INVALID;
        }
    } else if (runtime->passive_interface[0] != '\0') {
        if (df_copy_option(runtime->gvs_interface, sizeof(runtime->gvs_interface),
                           runtime->passive_interface) != DF_OK)
            return DF_ERR_INVALID;
    }
    if (runtime->config.active_host && runtime->indoor_ipaddr[0] == '\0') {
        uint8_t identity[6];

        if (df_gvs_identity_parse(runtime->gvs_local_address, identity) != DF_OK ||
            df_gvs_identity_unicast_ip(identity, runtime->indoor_ipaddr) != DF_OK) {
            return DF_ERR_INVALID;
        }
    }
    return df_config_validate(&runtime->config);
}

int df_runtime_config_load(const char *path, struct df_runtime_config *runtime) {
    FILE *file;
    char *contents;
    size_t length;
    int result;

    if (path == NULL || runtime == NULL) {
        return DF_ERR_INVALID;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return DF_ERR_IO;
    }
    contents = calloc(DF_RUNTIME_CONFIG_MAX_BYTES + 1, sizeof(*contents));
    if (contents == NULL) {
        (void)fclose(file);
        return DF_ERR_IO;
    }
    length = fread(contents, 1, DF_RUNTIME_CONFIG_MAX_BYTES, file);
    if (ferror(file) ||
        (length == DF_RUNTIME_CONFIG_MAX_BYTES && fgetc(file) != EOF)) {
        free(contents);
        (void)fclose(file);
        return DF_ERR_IO;
    }
    (void)fclose(file);
    contents[length] = '\0';
    result = df_runtime_config_parse(contents, runtime);
    free(contents);
    return result;
}
