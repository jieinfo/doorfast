#include "gvs_video_reassembly.h"

#include <stdlib.h>
#include <string.h>

int df_gvs_jpeg_validate(const uint8_t *data, size_t length)
{
    if (data == NULL || length < 4U) {
        return -1;
    }
    return data[0] == 0xff && data[1] == 0xd8 &&
        data[length - 2U] == 0xff && data[length - 1U] == 0xd9 ? 0 : -1;
}

void df_gvs_video_reassembly_init(struct df_gvs_video_reassembly *reassembly)
{
    if (reassembly != NULL) {
        memset(reassembly, 0, sizeof(*reassembly));
    }
}

void df_gvs_video_reassembly_reset(struct df_gvs_video_reassembly *reassembly)
{
    if (reassembly == NULL) {
        return;
    }
    free(reassembly->buffer);
    free(reassembly->received_map);
    memset(reassembly, 0, sizeof(*reassembly));
}

static int packet_geometry(const struct df_gvs_video_packet *packet,
                           size_t *offset, size_t *expected_length)
{
    uint32_t expected_count;

    if (packet == NULL || offset == NULL || expected_length == NULL ||
        packet->payload == NULL || packet->full_length == 0U ||
        packet->full_length > DF_GVS_VIDEO_MAX_FRAME ||
        packet->capacity == 0U || packet->capacity > 1200U ||
        packet->chunk_count == 0U || packet->chunk_index == 0U ||
        packet->chunk_index > packet->chunk_count) {
        return -1;
    }
    expected_count = (packet->full_length + packet->capacity - 1U) /
        packet->capacity;
    if (expected_count > UINT16_MAX || packet->chunk_count != expected_count) {
        return -1;
    }
    *offset = (size_t)(packet->chunk_index - 1U) * packet->capacity;
    if (*offset >= packet->full_length) {
        return -1;
    }
    *expected_length = packet->full_length - *offset;
    if (*expected_length > packet->capacity) {
        *expected_length = packet->capacity;
    }
    return packet->chunk_length == *expected_length ? 0 : -1;
}

static int start_frame(struct df_gvs_video_reassembly *reassembly,
                       const struct df_gvs_video_packet *packet)
{
    size_t map_size = ((size_t)packet->chunk_count + 7U) / 8U;
    uint8_t *buffer = malloc(packet->full_length);
    uint8_t *received_map = calloc(map_size, 1U);

    if (buffer == NULL || received_map == NULL) {
        free(buffer);
        free(received_map);
        return -1;
    }
    df_gvs_video_reassembly_reset(reassembly);
    reassembly->buffer = buffer;
    reassembly->received_map = received_map;
    reassembly->capacity = packet->full_length;
    reassembly->map_size = map_size;
    reassembly->frame_no = packet->frame_no;
    reassembly->chunk_count = packet->chunk_count;
    reassembly->chunk_capacity = packet->capacity;
    reassembly->full_length = packet->full_length;
    return 0;
}

int df_gvs_video_reassembly_push(struct df_gvs_video_reassembly *reassembly,
    const struct df_gvs_video_packet *packet, const uint8_t **output,
    size_t *output_length)
{
    size_t offset;
    size_t expected_length;
    size_t map_index;
    uint8_t map_mask;

    if (reassembly == NULL || packet == NULL || output == NULL ||
        output_length == NULL) {
        return DF_GVS_VIDEO_REASSEMBLY_ERROR;
    }
    *output = NULL;
    *output_length = 0;
    if (packet_geometry(packet, &offset, &expected_length) != 0) {
        return DF_GVS_VIDEO_REASSEMBLY_ERROR;
    }
    if (reassembly->buffer == NULL) {
        if (start_frame(reassembly, packet) != 0) {
            return DF_GVS_VIDEO_REASSEMBLY_ERROR;
        }
    } else if (packet->frame_no != reassembly->frame_no) {
        uint16_t distance = (uint16_t)(packet->frame_no - reassembly->frame_no);

        if (distance > UINT16_MAX / 2U) {
            return DF_GVS_VIDEO_REASSEMBLY_LATE;
        }
        if (start_frame(reassembly, packet) != 0) {
            return DF_GVS_VIDEO_REASSEMBLY_ERROR;
        }
    } else if (packet->full_length != reassembly->full_length ||
               packet->chunk_count != reassembly->chunk_count ||
               packet->capacity != reassembly->chunk_capacity) {
        return DF_GVS_VIDEO_REASSEMBLY_ERROR;
    }

    map_index = (packet->chunk_index - 1U) / 8U;
    map_mask = (uint8_t)(1U << ((packet->chunk_index - 1U) % 8U));
    if ((reassembly->received_map[map_index] & map_mask) != 0U) {
        if (memcmp(reassembly->buffer + offset, packet->payload,
                   expected_length) != 0) {
            return DF_GVS_VIDEO_REASSEMBLY_ERROR;
        }
        return DF_GVS_VIDEO_REASSEMBLY_DUPLICATE;
    }

    memcpy(reassembly->buffer + offset, packet->payload, expected_length);
    reassembly->received_map[map_index] |= map_mask;
    reassembly->received += expected_length;
    reassembly->received_chunks++;
    if (reassembly->received_chunks == reassembly->chunk_count) {
        if (reassembly->received != reassembly->full_length) {
            return DF_GVS_VIDEO_REASSEMBLY_ERROR;
        }
        reassembly->complete = true;
        *output = reassembly->buffer;
        *output_length = reassembly->received;
        return DF_GVS_VIDEO_REASSEMBLY_COMPLETE;
    }
    return DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE;
}
