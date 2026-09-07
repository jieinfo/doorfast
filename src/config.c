#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool df_delay_is_valid(int delay_seconds) {
    return delay_seconds >= -1 && delay_seconds <= 9;
}

int df_config_validate(const struct df_config *config) {
    if (config == NULL || !config->enabled) {
        return DF_OK;
    }
    if (config->brand == NULL || strcmp(config->brand, "gvs") != 0) {
        return DF_ERR_INVALID;
    }
    if (config->gvs_interface == NULL || config->gvs_interface[0] == '\0' ||
        !config->passive_only) {
        return DF_ERR_INVALID;
    }
    if (!df_delay_is_valid(config->unlock_delay_seconds) ||
        !df_delay_is_valid(config->hangup_delay_seconds)) {
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
