#include "config.h"
#include "gvs_identity.h"
#include "gvs_station.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

static bool df_delay_is_valid(int delay_seconds) {
    return delay_seconds >= -1 && delay_seconds <= 9;
}

static bool df_ipv4_is_valid(const char *text) {
    struct in_addr address;

    return text != NULL && inet_pton(AF_INET, text, &address) == 1;
}

static bool df_netmask_is_valid(const char *text);

static bool df_ipv4_is_unicast(const char *text) {
    struct in_addr address;
    uint32_t value;
    unsigned first_octet;

    if (!df_ipv4_is_valid(text) || inet_pton(AF_INET, text, &address) != 1)
        return false;
    value = ntohl(address.s_addr);
    first_octet = value >> 24;
    return value != 0U && value != 0xffffffffU && first_octet != 0U &&
           first_octet != 127U && first_octet < 224U;
}

static bool df_ipv4_is_directed_broadcast(const char *text, const char *local_ip,
                                          const char *netmask) {
    struct in_addr address;
    struct in_addr local_address;
    struct in_addr mask_address;
    uint32_t value;
    uint32_t local_value;
    uint32_t mask_value;

    if (!df_ipv4_is_valid(text) || !df_ipv4_is_valid(local_ip) ||
        !df_netmask_is_valid(netmask) || inet_pton(AF_INET, text, &address) != 1 ||
        inet_pton(AF_INET, local_ip, &local_address) != 1 ||
        inet_pton(AF_INET, netmask, &mask_address) != 1) return false;
    value = ntohl(address.s_addr);
    local_value = ntohl(local_address.s_addr);
    mask_value = ntohl(mask_address.s_addr);
    return mask_value < 0xfffffffeU &&
           value == ((local_value & mask_value) | ~mask_value);
}

static bool df_netmask_is_valid(const char *text) {
    struct in_addr address;
    uint32_t value;
    uint32_t inverse;

    if (!df_ipv4_is_valid(text) || inet_pton(AF_INET, text, &address) != 1)
        return false;
    value = ntohl(address.s_addr);
    inverse = ~value;
    return value != 0U && (inverse & (inverse + 1U)) == 0U;
}

static bool df_media_text_is_valid(const char *text, size_t maximum_length) {
    size_t index;
    size_t length;

    if (text == NULL) return false;
    length = strlen(text);
    if (length == 0 || length > maximum_length) return false;
    for (index = 0; index < length; index++) {
        unsigned char value = (unsigned char)text[index];

        if (value < 0x21U || value > 0x7eU) return false;
    }
    return true;
}

static bool df_media_host_is_valid(const char *host) {
    size_t index;
    size_t length;

    if (!df_media_text_is_valid(host, 63U)) return false;
    length = strlen(host);
    if (host[0] == '.' || host[0] == '-' || host[length - 1U] == '.' ||
        host[length - 1U] == '-') return false;
    for (index = 0; index < length; index++) {
        unsigned char value = (unsigned char)host[index];

        if (!(value == '.' || value == '-' ||
              (value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z'))) return false;
        if (index > 0 && host[index] == '.' && host[index - 1U] == '.') return false;
    }
    return true;
}

static bool df_media_identifier_is_valid(const char *text, size_t maximum_length) {
    size_t index;

    if (!df_media_text_is_valid(text, maximum_length)) return false;
    for (index = 0; text[index] != '\0'; index++) {
        unsigned char value = (unsigned char)text[index];

        if (!(value == '_' || value == '-' || value == '.' ||
              (value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z'))) return false;
    }
    return true;
}

static bool df_media_stream_name_is_valid(const char *text) {
    size_t index;

    if (!df_media_text_is_valid(text, 64U)) return false;
    for (index = 0; text[index] != '\0'; index++) {
        unsigned char value = (unsigned char)text[index];

        if (!(value == '_' || value == '-' ||
              (value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z'))) return false;
    }
    return true;
}

static bool df_media_port_is_valid(const char *text) {
    char *end = NULL;
    unsigned long value;

    if (!df_media_text_is_valid(text, 5U) ||
        strspn(text, "0123456789") != strlen(text)) return false;
    value = strtoul(text, &end, 10);
    return end != text && *end == '\0' && value >= 1UL && value <= 65535UL;
}

static bool df_media_relay_url_is_valid(const char *url) {
    const char *authority;
    const char *path;
    const char *port;
    char host[64];
    char port_text[6];
    size_t authority_length;
    size_t host_length;
    size_t index;

    if (url == NULL || url[0] == '\0') return true;
    if (strncmp(url, "http://", 7U) == 0) authority = url + 7;
    else if (strncmp(url, "https://", 8U) == 0) authority = url + 8;
    else return false;
    if (strlen(url) >= 256U) return false;
    path = strchr(authority, '/');
    authority_length = path == NULL ? strlen(authority) :
        (size_t)(path - authority);
    if (authority_length == 0U || authority_length > 69U) return false;
    for (index = 0U; index < strlen(authority); index++) {
        unsigned char value = (unsigned char)authority[index];

        if (value < 0x21U || value > 0x7eU || value == '?' ||
            value == '#' || value == '@') return false;
    }
    if (path != NULL) {
        for (index = 0U; path[index] != '\0'; index++) {
            unsigned char value = (unsigned char)path[index];

            if (value < 0x21U || value > 0x7eU || value == '?' ||
                value == '#' || value == '@') return false;
        }
    }
    port = memchr(authority, ':', authority_length);
    if (port != NULL && memchr(port + 1, ':',
                               authority_length - (size_t)(port + 1 - authority)) != NULL)
        return false;
    if (port == NULL) {
        if (authority_length >= sizeof(host)) return false;
        memcpy(host, authority, authority_length);
        host[authority_length] = '\0';
        return df_media_host_is_valid(host);
    }
    host_length = (size_t)(port - authority);
    if (host_length == 0 || host_length >= sizeof(host)) return false;
    memcpy(host, authority, host_length);
    host[host_length] = '\0';
    if ((size_t)(port + 1 - authority) >= authority_length ||
        authority_length - (size_t)(port + 1 - authority) >= sizeof(port_text))
        return false;
    memcpy(port_text, port + 1,
           authority_length - (size_t)(port + 1 - authority));
    port_text[authority_length - (size_t)(port + 1 - authority)] = '\0';
    return df_media_host_is_valid(host) &&
        df_media_port_is_valid(port_text);
}

static bool df_media_config_is_valid(const struct df_config *config) {
    const char *fps_values[] = {"5", "8", "10", "12", "15"};
    const struct df_media_config *media;
    size_t index;
    char fps[4];

    if (config == NULL || !config->media.enabled) return true;
    media = &config->media;
    if (media->station_address == NULL ||
        df_gvs_station_parse(media->station_address, (uint8_t[6]){0}) != DF_OK ||
        (media->station_ipv4 != NULL && media->station_ipv4[0] != '\0' &&
         (!df_ipv4_is_unicast(media->station_ipv4) ||
          df_ipv4_is_directed_broadcast(media->station_ipv4,
                                         config->indoor_ipaddr,
                                         config->indoor_netmask))) ||
        !df_media_host_is_valid(media->go2rtc_host) || media->go2rtc_port == 0U ||
        !df_media_stream_name_is_valid(media->stream_name) ||
        !df_media_identifier_is_valid(media->rtsp_username, 32U) ||
        media->credentials_path == NULL ||
        strcmp(media->credentials_path, DF_MEDIA_CREDENTIALS_PATH) != 0 ||
        !df_media_relay_url_is_valid(media->relay_url) ||
        media->encoder < DF_MEDIA_ENCODER_AUTO || media->encoder > DF_MEDIA_ENCODER_QSV ||
        media->resolution < DF_MEDIA_RESOLUTION_SOURCE ||
        media->resolution > DF_MEDIA_RESOLUTION_240X320 ||
        media->profile < DF_MEDIA_PROFILE_BASELINE ||
        media->profile > DF_MEDIA_PROFILE_MAIN ||
        media->max_encoders > 4U || media->bitrate_kbps < 256U ||
        media->bitrate_kbps > 2000U || media->min_free_kib < 131072U ||
        media->min_free_kib > 1048576U || media->preview_timeout_s < 15U ||
        media->preview_timeout_s > 600U || media->first_frame_timeout_s < 2U ||
        media->first_frame_timeout_s > 30U || media->publish_retries > 5U ||
        media->overload_policy < DF_MEDIA_OVERLOAD_REJECT_NEW ||
        media->overload_policy > DF_MEDIA_OVERLOAD_STOP_OLDEST_PREVIEW) return false;
    (void)snprintf(fps, sizeof(fps), "%u", (unsigned)media->fps);
    for (index = 0; index < DF_ARRAY_LEN(fps_values); index++) {
        if (strcmp(fps, fps_values[index]) == 0) return true;
    }
    return false;
}

int df_config_validate(const struct df_config *config) {
    uint8_t identity[6];

    if (config == NULL) {
        return DF_ERR_INVALID;
    }
    if (config->media.enabled && !config->enabled) return DF_ERR_INVALID;
    if (config->access_material != NULL && config->access_material[0] != '\0' &&
        (strlen(config->access_material) != 16U ||
         strspn(config->access_material, "0123456789abcdefABCDEF") != 16U))
        return DF_ERR_INVALID;
    if ((config->indoor_ipaddr != NULL && config->indoor_ipaddr[0] != '\0' &&
         !df_ipv4_is_valid(config->indoor_ipaddr)) ||
        (config->indoor_netmask != NULL && config->indoor_netmask[0] != '\0' &&
         !df_netmask_is_valid(config->indoor_netmask)))
        return DF_ERR_INVALID;
    if (!config->enabled) {
        return DF_OK;
    }
    if (config->brand == NULL || strcmp(config->brand, "gvs") != 0) {
        return DF_ERR_INVALID;
    }
    if (config->gvs_interface == NULL || config->gvs_interface[0] == '\0' ||
        config->gvs_local_address == NULL || config->gvs_local_address[0] == '\0' ||
        config->sync_state_path == NULL ||
        strncmp(config->sync_state_path, "/etc/config/doorfast-",
                sizeof("/etc/config/doorfast-") - 1U) != 0 ||
        config->sync_state_path[sizeof("/etc/config/doorfast-") - 1U] == '\0' ||
        strstr(config->sync_state_path, "..") != NULL ||
        (!config->passive_only && !config->active_host) ||
        (config->active_host &&
         (config->indoor_ipaddr == NULL || config->indoor_ipaddr[0] == '\0' ||
          config->indoor_netmask == NULL || config->indoor_netmask[0] == '\0')) ||
        df_gvs_identity_parse(config->gvs_local_address, identity) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (!df_delay_is_valid(config->unlock_delay_seconds) ||
        !df_delay_is_valid(config->hangup_delay_seconds) ||
        (config->media.enabled &&
         (!config->active_host || !df_media_config_is_valid(config)))) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

void df_config_redact(char *dst, size_t dst_size, const char *secret) {
    size_t visible_length;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (secret == NULL || secret[0] == '\0') {
        return;
    }
    visible_length = strlen(secret);
    if (visible_length > 4) {
        visible_length = 4;
    }
    if (visible_length >= dst_size) {
        visible_length = dst_size - 1;
    }
    memcpy(dst, secret, visible_length);
    dst[visible_length] = '\0';
    if (visible_length + 3 < dst_size) {
        (void)snprintf(dst + visible_length, dst_size - visible_length, "...");
    }
}

static int df_parse_legacy_delay(const char *value, int *destination) {
    char *end = NULL;
    long parsed;

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < -1 || parsed > 9) {
        return DF_ERR_INVALID;
    }
    *destination = (int)parsed;
    return DF_OK;
}

static int df_parse_legacy_boolean(const char *value, bool *destination) {
    if (strcmp(value, "0") == 0) {
        *destination = false;
        return DF_OK;
    }
    if (strcmp(value, "1") == 0) {
        *destination = true;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

int df_config_import_legacy(const char *legacy_uci, struct df_legacy_import *imported) {
    const char *cursor;
    enum { DF_SECTION_NONE, DF_SECTION_SETTINGS, DF_SECTION_AUTOMATION } section =
        DF_SECTION_NONE;

    if (legacy_uci == NULL || imported == NULL) {
        return DF_ERR_INVALID;
    }
    memset(imported, 0, sizeof(*imported));
    (void)snprintf(imported->brand, sizeof(imported->brand), "%s", "dnake");
    imported->config.brand = imported->brand;
    imported->config.capture_auto = true;
    imported->config.unlock_delay_seconds = -1;
    imported->config.hangup_delay_seconds = -1;

    cursor = legacy_uci;
    while (*cursor != '\0') {
        char line[256];
        char section_name[32];
        char option_name[32];
        char option_value[128];
        const char *line_end = strchr(cursor, '\n');
        size_t line_length = line_end == NULL ? strlen(cursor) : (size_t)(line_end - cursor);

        if (line_length >= sizeof(line)) {
            return DF_ERR_INVALID;
        }
        memcpy(line, cursor, line_length);
        line[line_length] = '\0';
        cursor = line_end == NULL ? cursor + line_length : line_end + 1;

        if (sscanf(line, " config doorlink '%31[^']'", section_name) == 1) {
            if (strcmp(section_name, "settings") == 0) {
                section = DF_SECTION_SETTINGS;
            } else if (strcmp(section_name, "automation") == 0) {
                section = DF_SECTION_AUTOMATION;
            } else {
                section = DF_SECTION_NONE;
            }
            continue;
        }
        if (sscanf(line, " option %31s '%127[^']'", option_name, option_value) != 2) {
            continue;
        }

        if (section == DF_SECTION_SETTINGS && strcmp(option_name, "brand") == 0) {
            if (strlen(option_value) >= sizeof(imported->brand)) {
                return DF_ERR_INVALID;
            }
            memcpy(imported->brand, option_value, strlen(option_value) + 1);
        } else if (section == DF_SECTION_SETTINGS && strcmp(option_name, "enabled") == 0) {
            if (df_parse_legacy_boolean(option_value, &imported->config.enabled) != DF_OK) {
                return DF_ERR_INVALID;
            }
        } else if (section == DF_SECTION_AUTOMATION && strcmp(option_name, "unlock") == 0) {
            if (df_parse_legacy_delay(option_value, &imported->config.unlock_delay_seconds) != DF_OK) {
                return DF_ERR_INVALID;
            }
        } else if (section == DF_SECTION_AUTOMATION && strcmp(option_name, "hangup") == 0) {
            if (df_parse_legacy_delay(option_value, &imported->config.hangup_delay_seconds) != DF_OK) {
                return DF_ERR_INVALID;
            }
        } else if (section == DF_SECTION_AUTOMATION && strcmp(option_name, "call_elev") == 0) {
            if (df_parse_legacy_boolean(option_value, &imported->config.call_elev) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
    }

    imported->config.enabled = false;
    return df_config_validate(&imported->config);
}
