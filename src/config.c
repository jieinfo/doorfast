#include "config.h"

#include <stdio.h>
#include <string.h>

static bool df_delay_is_valid(int delay_seconds) {
    return delay_seconds >= -1 && delay_seconds <= 9;
}

int df_config_validate(const struct df_config *config) {
    if (config == NULL || !config->enabled) {
        return DF_OK;
    }
    if (config->brand == NULL || strcmp(config->brand, "dnake") != 0) {
        return DF_ERR_INVALID;
    }
    if (!config->capture_auto &&
        (config->capture_interface == NULL || config->capture_interface[0] == '\0')) {
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
