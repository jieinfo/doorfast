#ifndef DOORFAST_CONFIG_H
#define DOORFAST_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "doorfast.h"

struct df_config {
    bool enabled;
    const char *brand;
    const char *capture_interface;
    bool capture_auto;
    bool capture_promiscuous;
    int unlock_delay_seconds;
    int hangup_delay_seconds;
    bool call_elev;
};

int df_config_validate(const struct df_config *config);
void df_config_redact(char *dst, size_t dst_size, const char *secret);

#endif
