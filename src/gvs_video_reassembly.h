#ifndef DOORFAST_GVS_VIDEO_REASSEMBLY_H
#define DOORFAST_GVS_VIDEO_REASSEMBLY_H
#include "gvs_media.h"
#include <stddef.h>
#include <stdint.h>
#define DF_GVS_VIDEO_MAX_FRAME (1024U * 1024U)
struct df_gvs_video_reassembly { uint16_t frame_no, chunk_count, next_chunk; uint32_t full_length; size_t received; uint8_t *buffer; size_t capacity; };
void df_gvs_video_reassembly_init(struct df_gvs_video_reassembly *);
void df_gvs_video_reassembly_reset(struct df_gvs_video_reassembly *);
int df_gvs_video_reassembly_push(struct df_gvs_video_reassembly *, const struct df_gvs_video_packet *, const uint8_t **, size_t *);
int df_gvs_jpeg_validate(const uint8_t *, size_t);
#endif
