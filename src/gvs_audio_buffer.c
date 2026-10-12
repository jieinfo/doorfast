#include "gvs_audio_buffer.h"

#include <string.h>

void df_gvs_audio_buffer_init(struct df_gvs_audio_buffer *b) {
    if (b == NULL) return;
    memset(b, 0, sizeof(*b));
    b->auto_bind_allowed = true;
}

void df_gvs_audio_buffer_reset(struct df_gvs_audio_buffer *b, uint64_t generation) {
    if (b == NULL) return;
    memset(b, 0, sizeof(*b));
    b->generation = generation;
}

static int bind_generation(struct df_gvs_audio_buffer *b, uint64_t generation) {
    if ((b->generation == 0U && !b->auto_bind_allowed) ||
        generation < b->generation) return DF_GVS_AUDIO_BUFFER_STALE;
    if (generation != b->generation) df_gvs_audio_buffer_reset(b, generation);
    return 0;
}

static void append_samples(struct df_gvs_audio_buffer *b, const uint8_t *data,
                           size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (b->length == DF_GVS_AUDIO_BUFFER_CAPACITY) {
            b->read_offset = (b->read_offset + 1U) % DF_GVS_AUDIO_BUFFER_CAPACITY;
            b->length--;
        }
        b->data[b->write_offset] = data == NULL ? 0xd5 : data[i];
        b->write_offset = (b->write_offset + 1U) % DF_GVS_AUDIO_BUFFER_CAPACITY;
        b->length++;
    }
    b->sample_count += count;
}

int df_gvs_audio_buffer_push(struct df_gvs_audio_buffer *b, const uint8_t *data,
    size_t length, uint16_t sequence, uint64_t generation, uint64_t timestamp_ms) {
    uint16_t distance = 1;
    int result;
    if (b == NULL || data == NULL || length == 0U || generation == 0U ||
        length > DF_GVS_AUDIO_BUFFER_CAPACITY) return DF_GVS_AUDIO_BUFFER_ERROR;
    result = bind_generation(b, generation);
    if (result != 0) return result;
    if (b->has_sequence) {
        distance = (uint16_t)(sequence - b->last_sequence);
        if (distance == 0U) {
            if (b->duplicate_packets == UINT64_MAX) return -1;
            b->duplicate_packets++;
            return DF_GVS_AUDIO_BUFFER_DUPLICATE;
        }
        if (distance > UINT16_MAX / 2U) {
            if (b->late_packets == UINT64_MAX) return -1;
            b->late_packets++;
            return DF_GVS_AUDIO_BUFFER_LATE;
        }
    }
    if (b->packet_count == UINT64_MAX || b->byte_count > UINT64_MAX - length ||
        b->sample_count > UINT64_MAX - length ||
        (distance > 1U && (b->sequence_gaps == UINT64_MAX ||
         b->missing_packets > UINT64_MAX - ((uint64_t)distance - 1U)))) return -1;
    if (distance > 1U) {
        b->sequence_gaps++;
        b->missing_packets += (uint64_t)distance - 1U;
    }
    append_samples(b, data, length);
    b->last_sequence = sequence;
    b->has_sequence = true;
    b->packet_count++;
    b->byte_count += length;
    b->last_timestamp_ms = timestamp_ms;
    return DF_GVS_AUDIO_BUFFER_ACCEPTED;
}

int df_gvs_audio_buffer_push_silence(struct df_gvs_audio_buffer *b, size_t count,
                                   uint64_t generation) {
    int result;
    if (b == NULL || count == 0U || count > DF_GVS_AUDIO_BUFFER_CAPACITY ||
        generation == 0U) return -1;
    result = bind_generation(b, generation);
    if (result != 0) return result;
    if (b->sample_count > UINT64_MAX - count ||
        b->concealed_samples > UINT64_MAX - count) return -1;
    append_samples(b, NULL, count);
    b->concealed_samples += count;
    return 0;
}

int df_gvs_audio_buffer_read(struct df_gvs_audio_buffer *b, uint8_t *output,
                           size_t capacity, size_t *length) {
    size_t i, count;
    if (b == NULL || output == NULL || length == NULL) return -1;
    count = b->length < capacity ? b->length : capacity;
    for (i = 0; i < count; i++) {
        output[i] = b->data[b->read_offset];
        b->read_offset = (b->read_offset + 1U) % DF_GVS_AUDIO_BUFFER_CAPACITY;
    }
    b->length -= count;
    *length = count;
    return 0;
}

int df_gvs_audio_buffer_copy(const struct df_gvs_audio_buffer *b, uint8_t *output,
                           size_t capacity, size_t *length) {
    size_t i, count;
    if (b == NULL || output == NULL || length == NULL) return -1;
    count = b->length < capacity ? b->length : capacity;
    for (i = 0; i < count; i++)
        output[i] = b->data[(b->read_offset + i) % DF_GVS_AUDIO_BUFFER_CAPACITY];
    *length = count;
    return 0;
}

static int copy_pending(const struct df_gvs_audio_buffer *b, uint8_t *output,
                        size_t capacity, size_t *length, bool tail) {
    uint64_t pending;
    size_t available, count, start, i;
    if (b == NULL || output == NULL || length == NULL ||
        b->snapshot_source_bytes > b->byte_count ||
        b->snapshot_source_samples > b->sample_count) return -1;
    pending = b->sample_count - b->snapshot_source_samples;
    available = pending > b->length ? b->length : (size_t)pending;
    count = available < capacity ? available : capacity;
    start = (b->write_offset + DF_GVS_AUDIO_BUFFER_CAPACITY -
             (tail ? count : available)) % DF_GVS_AUDIO_BUFFER_CAPACITY;
    for (i = 0; i < count; i++)
        output[i] = b->data[(start + i) % DF_GVS_AUDIO_BUFFER_CAPACITY];
    *length = count;
    return 0;
}

int df_gvs_audio_buffer_copy_pending(const struct df_gvs_audio_buffer *b,
    uint8_t *output, size_t capacity, size_t *length) {
    return copy_pending(b, output, capacity, length, false);
}

int df_gvs_audio_buffer_copy_pending_tail(const struct df_gvs_audio_buffer *b,
    uint8_t *output, size_t capacity, size_t *length) {
    return copy_pending(b, output, capacity, length, true);
}

int df_gvs_audio_buffer_mark_snapshot(struct df_gvs_audio_buffer *b,
    uint64_t generation, size_t source_samples, uint64_t timestamp_ms) {
    uint64_t pending;
    if (b == NULL || generation == 0U || generation != b->generation ||
        source_samples == 0U || source_samples > DF_GVS_AUDIO_BUFFER_CAPACITY ||
        b->snapshot_source_bytes > b->byte_count ||
        b->snapshot_source_samples > b->sample_count) return -1;
    pending = b->sample_count - b->snapshot_source_samples;
    if (source_samples > (pending > b->length ? b->length : (size_t)pending))
        return -1;
    b->snapshot_previous_packet_count = b->snapshot_packet_count;
    b->snapshot_packet_count = b->packet_count;
    b->snapshot_source_bytes = b->byte_count;
    b->snapshot_source_samples = b->sample_count;
    b->snapshot_dropped_bytes = pending - source_samples;
    b->snapshot_timestamp_ms = timestamp_ms;
    b->snapshot_bytes = 44U + source_samples * 2U;
    b->snapshot_ready = true;
    return 0;
}

int df_gvs_audio_buffer_status(const struct df_gvs_audio_buffer *b,
                             struct df_gvs_audio_status *s) {
    if (b == NULL || s == NULL) return -1;
    s->generation = b->generation;
    s->packet_count = b->packet_count;
    s->byte_count = b->byte_count;
    s->sample_count = b->sample_count;
    s->concealed_samples = b->concealed_samples;
    s->sequence_gaps = b->sequence_gaps;
    s->missing_packets = b->missing_packets;
    s->duplicate_packets = b->duplicate_packets;
    s->late_packets = b->late_packets;
    s->last_timestamp_ms = b->last_timestamp_ms;
    s->snapshot_packet_count = b->snapshot_packet_count;
    s->snapshot_previous_packet_count = b->snapshot_previous_packet_count;
    s->snapshot_source_bytes = b->snapshot_source_bytes;
    s->snapshot_source_samples = b->snapshot_source_samples;
    s->snapshot_dropped_bytes = b->snapshot_dropped_bytes;
    s->snapshot_timestamp_ms = b->snapshot_timestamp_ms;
    s->buffered_bytes = b->length;
    s->snapshot_bytes = b->snapshot_bytes;
    s->ready = b->length > 0;
    s->snapshot_ready = b->snapshot_ready;
    return 0;
}
