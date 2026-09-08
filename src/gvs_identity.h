#ifndef DOORFAST_GVS_IDENTITY_H
#define DOORFAST_GVS_IDENTITY_H

#include <stdint.h>

#include "doorfast.h"
#include "gvs_frame.h"

int df_gvs_identity_parse(const char *address, uint8_t identity[6]);
int df_gvs_frame_is_for_identity(const struct df_gvs_frame *frame,
                                 const uint8_t identity[6]);

#endif
