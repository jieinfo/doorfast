#ifndef DOORFAST_GVS_MEDIA_LIFECYCLE_H
#define DOORFAST_GVS_MEDIA_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "gvs_session.h"

struct df_gvs_media_lifecycle {
    uint64_t generation;
    bool active;
    bool initialized;
};

struct df_gvs_media_lifecycle_result {
    uint64_t generation;
    bool active;
    bool clear;
    bool started;
    bool ended;
};

void df_gvs_media_lifecycle_init(struct df_gvs_media_lifecycle *);
int df_gvs_media_lifecycle_sync(struct df_gvs_media_lifecycle *,
    const struct df_gvs_session *, struct df_gvs_media_lifecycle_result *);

#endif
