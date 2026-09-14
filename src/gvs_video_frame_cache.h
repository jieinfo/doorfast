#ifndef DOORFAST_GVS_VIDEO_FRAME_CACHE_H
#define DOORFAST_GVS_VIDEO_FRAME_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_GVS_VIDEO_CACHE_MAX (1024U * 1024U)

struct df_gvs_video_frame_cache {
    uint8_t *data;
    size_t length;
    size_t capacity;
    uint64_t generation;
    uint64_t timestamp_ms;
    uint16_t frame_no;
    bool valid;
};

struct df_gvs_video_status {
    size_t bytes;
    uint64_t generation;
    uint64_t timestamp_ms;
    uint16_t frame_no;
    bool ready;
};

void df_gvs_video_frame_cache_init(struct df_gvs_video_frame_cache *);
void df_gvs_video_frame_cache_reset(struct df_gvs_video_frame_cache *);
void df_gvs_video_frame_cache_invalidate(struct df_gvs_video_frame_cache *);
int df_gvs_video_frame_cache_store(struct df_gvs_video_frame_cache *,
    const uint8_t *, size_t, uint64_t, uint16_t, uint64_t);
int df_gvs_video_frame_cache_snapshot(const struct df_gvs_video_frame_cache *,
    const uint8_t **, size_t *, uint64_t *, uint16_t *, uint64_t *);
int df_gvs_video_frame_cache_status(const struct df_gvs_video_frame_cache *,
    struct df_gvs_video_status *);

#endif
