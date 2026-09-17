#ifndef DOORFAST_MEDIA_CAPACITY_H
#define DOORFAST_MEDIA_CAPACITY_H

#include <stdbool.h>

#include "config.h"

struct df_media_encoder_probe {
    bool software_available;
    bool vaapi_available;
    bool qsv_available;
};

unsigned df_media_effective_capacity(unsigned requested, unsigned resource_limit,
                                     unsigned protocol_limit);
int df_media_encoder_select(enum df_media_encoder requested,
                            const struct df_media_encoder_probe *,
                            enum df_media_encoder *selected);

#endif
