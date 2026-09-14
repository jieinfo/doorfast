#include "gvs_video_frame_cache.h"

#include <stdlib.h>
#include <string.h>

void df_gvs_video_frame_cache_init(struct df_gvs_video_frame_cache *cache)
{
    if (cache != NULL) {
        memset(cache, 0, sizeof(*cache));
    }
}

void df_gvs_video_frame_cache_reset(struct df_gvs_video_frame_cache *cache)
{
    if (cache == NULL) {
        return;
    }
    free(cache->data);
    memset(cache, 0, sizeof(*cache));
}

void df_gvs_video_frame_cache_invalidate(struct df_gvs_video_frame_cache *cache)
{
    if (cache == NULL) {
        return;
    }
    cache->length = 0;
    cache->generation = 0;
    cache->frame_no = 0;
    cache->timestamp_ms = 0;
    cache->valid = false;
}

int df_gvs_video_frame_cache_store(struct df_gvs_video_frame_cache *cache,
    const uint8_t *data, size_t length, uint64_t generation,
    uint16_t frame_no, uint64_t timestamp_ms)
{
    uint8_t *next;

    if (cache == NULL || data == NULL || length == 0U ||
        length > DF_GVS_VIDEO_CACHE_MAX || generation == 0U) {
        return -1;
    }
    if (cache->capacity < length) {
        next = realloc(cache->data, length);
        if (next == NULL) {
            return -1;
        }
        cache->data = next;
        cache->capacity = length;
    }
    memcpy(cache->data, data, length);
    cache->length = length;
    cache->generation = generation;
    cache->frame_no = frame_no;
    cache->timestamp_ms = timestamp_ms;
    cache->valid = true;
    return 0;
}

int df_gvs_video_frame_cache_snapshot(
    const struct df_gvs_video_frame_cache *cache, const uint8_t **data,
    size_t *length, uint64_t *generation, uint16_t *frame_no,
    uint64_t *timestamp_ms)
{
    if (cache == NULL || data == NULL || length == NULL ||
        generation == NULL || frame_no == NULL || timestamp_ms == NULL ||
        !cache->valid) {
        return -1;
    }
    *data = cache->data;
    *length = cache->length;
    *generation = cache->generation;
    *frame_no = cache->frame_no;
    *timestamp_ms = cache->timestamp_ms;
    return 0;
}

int df_gvs_video_frame_cache_status(
    const struct df_gvs_video_frame_cache *cache,
    struct df_gvs_video_status *status)
{
    if (cache == NULL || status == NULL) {
        return -1;
    }
    memset(status, 0, sizeof(*status));
    if (cache->valid) {
        status->bytes = cache->length;
        status->generation = cache->generation;
        status->frame_no = cache->frame_no;
        status->timestamp_ms = cache->timestamp_ms;
        status->ready = true;
    }
    return 0;
}
