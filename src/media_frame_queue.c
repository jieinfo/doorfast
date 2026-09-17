#include "media_frame_queue.h"

#include <stdlib.h>
#include <string.h>

#include "doorfast.h"

int df_media_frame_queue_init(struct df_media_frame_queue *queue, size_t capacity,
                              size_t maximum_frame_length)
{
    size_t index;

    if (queue == NULL || capacity == 0U ||
        capacity > DF_MEDIA_FRAME_QUEUE_MAX_ENTRIES || maximum_frame_length == 0U ||
        maximum_frame_length > DF_GVS_VIDEO_MAX_FRAME) return DF_ERR_INVALID;
    memset(queue, 0, sizeof(*queue));
    for (index = 0; index < capacity; index++) {
        queue->entries[index].data = malloc(maximum_frame_length);
        if (queue->entries[index].data == NULL) {
            df_media_frame_queue_destroy(queue);
            return DF_ERR_IO;
        }
    }
    queue->capacity = capacity;
    queue->maximum_frame_length = maximum_frame_length;
    return DF_OK;
}

void df_media_frame_queue_destroy(struct df_media_frame_queue *queue)
{
    size_t index;

    if (queue == NULL) return;
    for (index = 0; index < DF_MEDIA_FRAME_QUEUE_MAX_ENTRIES; index++) {
        free(queue->entries[index].data);
    }
    memset(queue, 0, sizeof(*queue));
}

int df_media_frame_queue_push(struct df_media_frame_queue *queue,
                              const uint8_t *jpeg, size_t length,
                              uint64_t generation, uint64_t timestamp_ms)
{
    struct df_media_frame_queue_entry *entry;

    if (queue == NULL || jpeg == NULL || length == 0U || generation == 0U ||
        queue->capacity == 0U || queue->maximum_frame_length == 0U ||
        length > queue->maximum_frame_length) return DF_ERR_INVALID;
    if (queue->generation == 0U) queue->generation = generation;
    if (queue->generation != generation) return DF_ERR_INVALID;
    if (queue->count == queue->capacity) {
        queue->read = (queue->read + 1U) % queue->capacity;
        queue->count--;
        queue->dropped_oldest++;
    }
    entry = &queue->entries[queue->write];
    memcpy(entry->data, jpeg, length);
    entry->length = length;
    entry->generation = generation;
    entry->timestamp_ms = timestamp_ms;
    queue->write = (queue->write + 1U) % queue->capacity;
    queue->count++;
    return DF_OK;
}

int df_media_frame_queue_pop(struct df_media_frame_queue *queue,
                             struct df_media_frame *frame)
{
    struct df_media_frame_queue_entry *entry;

    if (queue == NULL || frame == NULL || queue->capacity == 0U ||
        queue->count == 0U) return DF_ERR_INVALID;
    entry = &queue->entries[queue->read];
    frame->data = entry->data;
    frame->length = entry->length;
    frame->generation = entry->generation;
    frame->timestamp_ms = entry->timestamp_ms;
    entry->length = 0;
    entry->generation = 0;
    entry->timestamp_ms = 0;
    queue->read = (queue->read + 1U) % queue->capacity;
    queue->count--;
    return DF_OK;
}
