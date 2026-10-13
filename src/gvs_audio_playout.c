#include "gvs_audio_playout.h"

#include <string.h>

int df_gvs_audio_playout_init(struct df_gvs_audio_playout *p,
    struct df_gvs_audio_buffer *buffer, df_gvs_audio_publish_fn publish,
    void *context) {
    if (p == NULL || buffer == NULL || publish == NULL || buffer->length != 0U)
        return -1;
    memset(p, 0, sizeof(*p));
    p->buffer = buffer;
    p->publish = publish;
    p->context = context;
    p->generation = buffer->generation;
    p->auto_bind_allowed = buffer->auto_bind_allowed;
    return 0;
}

void df_gvs_audio_playout_reset(struct df_gvs_audio_playout *p,
                              uint64_t generation) {
    if (p == NULL || p->buffer == NULL) return;
    p->generation = generation;
    p->oldest_pending_ms = 0;
    p->next_retry_ms = 0;
    p->has_pending = false;
    p->discontinuity = false;
    p->concealed_gap_pending = false;
    p->auto_bind_allowed = false;
    df_gvs_audio_buffer_reset(p->buffer, generation);
}

int df_gvs_audio_playout_push(struct df_gvs_audio_playout *p,
                            const struct df_gvs_audio_emission *emission) {
    int result;
    if (p == NULL || p->buffer == NULL || p->publish == NULL || emission == NULL ||
        emission->generation == 0U || emission->count == 0U ||
        emission->count > DF_GVS_AUDIO_BUFFER_CAPACITY ||
        (!emission->synthetic && emission->samples == NULL)) return -1;
    if (p->generation == 0U) {
        if (!p->auto_bind_allowed) return 1;
        df_gvs_audio_playout_reset(p, emission->generation);
    }
    if (emission->generation != p->generation) return 1;
    if (p->buffer->generation != p->generation) return -1;
    if (emission->synthetic)
        result = df_gvs_audio_buffer_push_silence(p->buffer, emission->count,
                                                emission->generation);
    else if (p->concealed_gap_pending && emission->discontinuity)
        result = df_gvs_audio_buffer_push_after_jitter(p->buffer,
            emission->samples, emission->count, emission->sequence,
            emission->generation, emission->timestamp_ms);
    else
        result = df_gvs_audio_buffer_push(p->buffer, emission->samples,
            emission->count, emission->sequence, emission->generation,
            emission->timestamp_ms);
    if (result != 0) return result < 0 ? result : 1;
    if (!p->has_pending) {
        p->oldest_pending_ms = emission->timestamp_ms;
        p->has_pending = true;
    }
    if (emission->synthetic) p->concealed_gap_pending = true;
    p->discontinuity = p->discontinuity || emission->discontinuity;
    if (!emission->synthetic) p->concealed_gap_pending = false;
    return 0;
}

int df_gvs_audio_playout_tick(struct df_gvs_audio_playout *p, uint64_t now_ms) {
    struct df_gvs_audio_buffer *b;
    struct df_gvs_audio_chunk chunk;
    uint8_t samples[1600];
    uint64_t pending_samples;
    size_t count;
    bool due;
    int result;
    if (p == NULL || p->buffer == NULL || p->publish == NULL) return -1;
    if (p->generation == 0U) return 0;
    b = p->buffer;
    /* These cover every mark_snapshot precondition before the publisher can
     * perform an external side effect. The callback must preserve this state. */
    if (b->generation != p->generation ||
        b->snapshot_source_bytes > b->byte_count ||
        b->snapshot_source_samples > b->sample_count ||
        b->snapshot_packet_count > b->packet_count ||
        b->length > DF_GVS_AUDIO_BUFFER_CAPACITY ||
        b->write_offset >= DF_GVS_AUDIO_BUFFER_CAPACITY) return -1;
    pending_samples = b->sample_count - b->snapshot_source_samples;
    if (!p->has_pending || pending_samples == 0U ||
        b->packet_count == b->snapshot_packet_count) return 0;
    if (p->next_retry_ms != 0U && now_ms < p->next_retry_ms) return 0;
    due = pending_samples >= 800U ||
        (now_ms >= p->oldest_pending_ms && now_ms - p->oldest_pending_ms >= 100U);
    if (!due) return 0;
    /* Even a successful file write must not precede a predictable scheduling
     * error: reserve a representable failure deadline before calling publish. */
    if (now_ms > UINT64_MAX - 100U) return -1;
    result = df_gvs_audio_buffer_copy_pending_tail(b, samples, sizeof(samples),
                                                 &count);
    if (result != 0 || count == 0U) return -1;
    chunk.generation = p->generation;
    chunk.previous_revision = b->snapshot_packet_count;
    chunk.revision = b->packet_count;
    chunk.timestamp_ms = now_ms;
    chunk.sample_count = count;
    chunk.dropped_samples = pending_samples - count;
    chunk.discontinuity = p->discontinuity || chunk.dropped_samples != 0U;
    result = p->publish(samples, &chunk, p->context);
    if (result != 0) {
        p->next_retry_ms = now_ms + 100U;
        return result;
    }
    result = df_gvs_audio_buffer_mark_snapshot(b, p->generation, count, now_ms);
    if (result != 0) return result;
    b->snapshot_discontinuity = chunk.discontinuity;
    p->has_pending = false;
    p->oldest_pending_ms = 0;
    p->next_retry_ms = 0;
    p->discontinuity = false;
    return 0;
}
