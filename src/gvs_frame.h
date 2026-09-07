#ifndef DOORFAST_GVS_FRAME_H
#define DOORFAST_GVS_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "event.h"

struct df_gvs_frame {
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t family;
    uint8_t opcode;
    uint8_t status;
};

int df_gvs_frame_parse(const uint8_t *data, size_t length,
                       struct df_gvs_frame *frame, struct df_event *event);

#endif
