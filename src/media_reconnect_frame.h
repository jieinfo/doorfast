#ifndef DOORFAST_MEDIA_RECONNECT_FRAME_H
#define DOORFAST_MEDIA_RECONNECT_FRAME_H

#include <stddef.h>
#include <stdint.h>

struct df_media_reconnect_frame {
    uint8_t *data;
    size_t length;
    uint16_t width;
    uint16_t height;
};

int df_media_reconnect_frame_create(uint16_t width, uint16_t height,
    struct df_media_reconnect_frame *out);
int df_media_reconnect_frame_freeze(const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, struct df_media_reconnect_frame *out);
void df_media_reconnect_frame_destroy(struct df_media_reconnect_frame *frame);

#endif
