#ifndef DOORFAST_GVS_MEDIA_ADMISSION_H
#define DOORFAST_GVS_MEDIA_ADMISSION_H

#include <stdint.h>

#include "gvs_session.h"

enum df_gvs_media_admission {
    DF_GVS_MEDIA_ADMISSION_ERROR = -1,
    DF_GVS_MEDIA_REJECT_INACTIVE = 0,
    DF_GVS_MEDIA_ACCEPTED = 1,
    DF_GVS_MEDIA_REJECT_DESTINATION = 2,
    DF_GVS_MEDIA_REJECT_PEER = 3,
};

enum df_gvs_media_admission df_gvs_media_admit(
    const struct df_gvs_session *, const uint8_t [6],
    const uint8_t [6], const uint8_t [6]);

#endif
