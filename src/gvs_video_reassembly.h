#ifndef DOORFAST_GVS_VIDEO_REASSEMBLY_H
#define DOORFAST_GVS_VIDEO_REASSEMBLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_media.h"

#define DF_GVS_VIDEO_MAX_FRAME (1024U * 1024U)

enum df_gvs_video_reassembly_result {
    DF_GVS_VIDEO_REASSEMBLY_ERROR = -1,
    DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE = 0,
    DF_GVS_VIDEO_REASSEMBLY_COMPLETE = 1,
    DF_GVS_VIDEO_REASSEMBLY_DUPLICATE = 2,
    DF_GVS_VIDEO_REASSEMBLY_LATE = 3,
};

struct df_gvs_video_reassembly {
    uint16_t frame_no;
    uint16_t chunk_count;
    uint16_t chunk_capacity;
    uint32_t full_length;
    size_t received;
    size_t received_chunks;
    uint8_t *buffer;
    uint8_t *received_map;
    size_t capacity;
    size_t map_size;
    bool complete;
};

void df_gvs_video_reassembly_init(struct df_gvs_video_reassembly *);
void df_gvs_video_reassembly_reset(struct df_gvs_video_reassembly *);
int df_gvs_video_reassembly_push(struct df_gvs_video_reassembly *,
    const struct df_gvs_video_packet *, const uint8_t **, size_t *);
int df_gvs_jpeg_validate(const uint8_t *, size_t);

#endif
