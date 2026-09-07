#include "gvs_frame.h"

#include <string.h>

enum {
    DF_GVS_HEADER_SIZE = 10,
    DF_GVS_DESTINATION_OFFSET = 10,
    DF_GVS_SOURCE_OFFSET = 16,
    DF_GVS_OPAQUE_OFFSET = 22,
    DF_GVS_OPAQUE_SIZE = 16,
    DF_GVS_FAMILY_OFFSET = DF_GVS_OPAQUE_OFFSET + DF_GVS_OPAQUE_SIZE,
    DF_GVS_MINIMUM_LENGTH = DF_GVS_FAMILY_OFFSET + 3,
};

static const uint8_t df_gvs_header[] = {'G', 'V', 'S', 'G', 'V', 'S',
                                        0xA5, 0xA5, 0xA5, 0xA5};

static enum df_event_type df_gvs_event_type(uint8_t family, uint8_t opcode) {
    if (family == 0x03 && opcode == 0x04) {
        return DF_EVENT_PREVIEW_STARTED;
    }
    if (family == 0x03 && opcode == 0x86) {
        return DF_EVENT_STATION_OBSERVED;
    }
    if (family == 0x03 && opcode == 0x50) {
        return DF_EVENT_SESSION_ESTABLISHED;
    }
    if (family == 0x04 && opcode == 0x89) {
        return DF_EVENT_UNLOCK_RESULT_OBSERVED;
    }
    return DF_EVENT_UNKNOWN;
}

int df_gvs_frame_parse(const uint8_t *data, size_t length,
                       struct df_gvs_frame *frame, struct df_event *event) {
    if (data == NULL || frame == NULL || event == NULL || length < DF_GVS_MINIMUM_LENGTH) {
        return DF_ERR_INVALID;
    }
    if (memcmp(data, df_gvs_header, sizeof(df_gvs_header)) != 0) {
        return DF_ERR_INVALID;
    }

    memset(frame, 0, sizeof(*frame));
    memset(event, 0, sizeof(*event));
    memcpy(frame->destination, data + DF_GVS_DESTINATION_OFFSET, sizeof(frame->destination));
    memcpy(frame->source, data + DF_GVS_SOURCE_OFFSET, sizeof(frame->source));
    frame->family = data[DF_GVS_FAMILY_OFFSET];
    frame->opcode = data[DF_GVS_FAMILY_OFFSET + 1];
    frame->status = data[DF_GVS_FAMILY_OFFSET + 2];
    event->type = df_gvs_event_type(frame->family, frame->opcode);
    return DF_OK;
}
