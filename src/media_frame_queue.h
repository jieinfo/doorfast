#ifndef DOORFAST_MEDIA_FRAME_QUEUE_H
#define DOORFAST_MEDIA_FRAME_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "gvs_video_reassembly.h"

#define DF_MEDIA_FRAME_QUEUE_MAX_ENTRIES 4U

struct df_media_frame {
    const uint8_t *data;
    size_t length;
    uint64_t generation;
    uint64_t timestamp_ms;
};

struct df_media_frame_queue_entry {
    uint8_t *data;
    size_t length;
    uint64_t generation;
    uint64_t timestamp_ms;
};

struct df_media_frame_queue {
    struct df_media_frame_queue_entry entries[DF_MEDIA_FRAME_QUEUE_MAX_ENTRIES];
    size_t read;
    size_t write;
    size_t count;
    size_t capacity;
    size_t maximum_frame_length;
    uint64_t generation;
    unsigned dropped_oldest;
};

int df_media_frame_queue_init(struct df_media_frame_queue *, size_t capacity,
                              size_t maximum_frame_length);
void df_media_frame_queue_destroy(struct df_media_frame_queue *);
int df_media_frame_queue_push(struct df_media_frame_queue *, const uint8_t *jpeg,
                              size_t length, uint64_t generation,
                              uint64_t timestamp_ms);
int df_media_frame_queue_pop(struct df_media_frame_queue *, struct df_media_frame *);

#endif
