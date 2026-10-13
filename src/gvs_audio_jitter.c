#include "gvs_audio_jitter.h"

#include <string.h>

static int reject_late_packet(struct df_gvs_audio_jitter *jitter)
{
    jitter->late_packets++;
    return 1;
}

static size_t nearest_slot(const struct df_gvs_audio_jitter *jitter)
{
    size_t nearest = DF_GVS_AUDIO_JITTER_SLOTS;
    uint16_t distance = UINT16_MAX;
    for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++) {
        uint16_t candidate = (uint16_t)(jitter->slots[i].sequence -
                                        jitter->expected_sequence);
        if (jitter->slots[i].occupied && candidate < distance) {
            nearest = i;
            distance = candidate;
        }
    }
    return nearest;
}

static uint64_t gap_deadline(const struct df_gvs_audio_jitter *jitter)
{
    uint64_t arrival = UINT64_MAX;
    for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++) {
        if (jitter->slots[i].occupied && jitter->slots[i].arrival_ms < arrival)
            arrival = jitter->slots[i].arrival_ms;
    }
    return arrival > UINT64_MAX - DF_GVS_AUDIO_JITTER_GAP_MS
        ? UINT64_MAX : arrival + DF_GVS_AUDIO_JITTER_GAP_MS;
}

static int emit(struct df_gvs_audio_jitter *jitter,
                const uint8_t *samples, size_t count, uint16_t sequence,
                uint64_t now_ms, bool synthetic, bool discontinuity)
{
    const struct df_gvs_audio_emission emission = {
        .samples = samples,
        .count = count,
        .sequence = sequence,
        .generation = jitter->generation,
        .timestamp_ms = now_ms,
        .synthetic = synthetic,
        .discontinuity = discontinuity,
    };
    int result = jitter->emit(&emission, jitter->context);
    return result > 0 ? -1 : result;
}

static int drain(struct df_gvs_audio_jitter *jitter, uint64_t now_ms, bool force)
{
    /* Each pass retires one of at most four slots, or stops on wait/failure. */
    for (size_t pass = 0; pass < DF_GVS_AUDIO_JITTER_SLOTS; pass++) {
        size_t index = nearest_slot(jitter);
        struct df_gvs_audio_jitter_slot *slot;
        uint16_t missing;
        bool discontinuity = false;
        int result;
        if (index == DF_GVS_AUDIO_JITTER_SLOTS) {
            jitter->gap_deadline_ms = 0;
            return 0;
        }
        slot = &jitter->slots[index];
        missing = (uint16_t)(slot->sequence - jitter->expected_sequence);
        if (missing != 0U) {
            size_t silence_count;
            jitter->gap_deadline_ms = gap_deadline(jitter);
            if (!force && jitter->gap_synthetic_samples == 0U &&
                now_ms < jitter->gap_deadline_ms) return 0;
            /* Compare before multiplying, bounding even a huge packet gap. */
            discontinuity = missing > DF_GVS_AUDIO_JITTER_MAX_SILENCE /
                                       jitter->last_real_samples;
            silence_count = discontinuity ? DF_GVS_AUDIO_JITTER_MAX_SILENCE
                : (size_t)missing * jitter->last_real_samples;
            if (jitter->gap_synthetic_samples == 0U) {
                uint8_t silence[DF_GVS_AUDIO_JITTER_MAX_SILENCE];
                memset(silence, 0xd5, silence_count);
                result = emit(jitter, silence, silence_count,
                              jitter->expected_sequence, now_ms, true,
                              discontinuity);
                if (result != 0) return result;
                jitter->gap_synthetic_samples = silence_count;
            }
        }
        result = emit(jitter, slot->samples, slot->count, slot->sequence,
                      now_ms, false, discontinuity);
        if (result != 0) return result;
        jitter->expected_sequence = (uint16_t)(slot->sequence + 1U);
        jitter->last_real_samples = slot->count;
        slot->occupied = false;
        jitter->gap_synthetic_samples = 0;
        jitter->gap_deadline_ms = 0;
        force = false;
    }
    return 0;
}

int df_gvs_audio_jitter_init(struct df_gvs_audio_jitter *jitter,
                            df_gvs_audio_jitter_emit_fn emit, void *context)
{
    if (jitter == NULL || emit == NULL) return -1;
    memset(jitter, 0, sizeof(*jitter));
    jitter->emit = emit;
    jitter->context = context;
    jitter->auto_bind_allowed = true;
    return 0;
}

void df_gvs_audio_jitter_reset(struct df_gvs_audio_jitter *jitter,
                              uint64_t generation)
{
    if (jitter == NULL) return;
    df_gvs_audio_jitter_emit_fn callback = jitter->emit;
    void *context = jitter->context;
    memset(jitter, 0, sizeof(*jitter));
    jitter->emit = callback;
    jitter->context = context;
    jitter->generation = generation;
}

int df_gvs_audio_jitter_push(struct df_gvs_audio_jitter *jitter,
    const uint8_t *samples, size_t count, uint16_t sequence,
    uint64_t generation, uint64_t now_ms)
{
    size_t free_slot = DF_GVS_AUDIO_JITTER_SLOTS;
    uint16_t distance;
    if (jitter == NULL || jitter->emit == NULL || samples == NULL ||
        count == 0U || count > DF_GVS_AUDIO_JITTER_MAX_SAMPLES ||
        generation == 0U) return -1;
    if (jitter->generation != 0U && generation != jitter->generation) return 1;
    if (jitter->generation == 0U && !jitter->auto_bind_allowed) return 1;
    if (now_ms < jitter->last_timestamp_ms) return -1;
    jitter->last_timestamp_ms = now_ms;
    if (jitter->generation == 0U) jitter->generation = generation;
    if (!jitter->has_sequence) {
        jitter->expected_sequence = sequence;
        jitter->has_sequence = true;
    }
    for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++) {
        if (jitter->slots[i].occupied && jitter->slots[i].sequence == sequence) {
            jitter->duplicate_packets++;
            return 1;
        }
        if (!jitter->slots[i].occupied) free_slot = i;
    }
    if (jitter->last_real_samples != 0U &&
        sequence == (uint16_t)(jitter->expected_sequence - 1U)) {
        jitter->duplicate_packets++;
        return 1;
    }
    distance = (uint16_t)(sequence - jitter->expected_sequence);
    if (distance >= 32768U) return reject_late_packet(jitter);
    /* Once silence was accepted, newly arriving concealed packets are late.
     * This keeps the closest queued real packet pinned across callback retries.
     */
    if (jitter->gap_synthetic_samples != 0U) {
        size_t nearest = nearest_slot(jitter);
        uint16_t concealed = (uint16_t)(jitter->slots[nearest].sequence -
                                        jitter->expected_sequence);
        if (distance < concealed) return reject_late_packet(jitter);
    }
    if (free_slot == DF_GVS_AUDIO_JITTER_SLOTS) {
        if (distance == 0U) {
            /* Repair the gap directly without allocating a fifth waiting slot.
             * On failure the caller still owns this input and may push it again.
             */
            int result = emit(jitter, samples, count, sequence, now_ms, false, false);
            if (result != 0) return result;
            jitter->expected_sequence = (uint16_t)(sequence + 1U);
            jitter->last_real_samples = count;
            jitter->gap_deadline_ms = 0;
            return drain(jitter, now_ms, false);
        }
        int result = drain(jitter, now_ms, true);
        if (result != 0) return result;
        distance = (uint16_t)(sequence - jitter->expected_sequence);
        if (distance >= 32768U) return reject_late_packet(jitter);
        for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++)
            if (!jitter->slots[i].occupied) free_slot = i;
    }
    struct df_gvs_audio_jitter_slot *slot = &jitter->slots[free_slot];
    memcpy(slot->samples, samples, count);
    slot->sequence = sequence;
    slot->count = count;
    slot->arrival_ms = now_ms;
    slot->occupied = true;
    if (distance != 0U) jitter->reordered_packets++;
    return drain(jitter, now_ms, false);
}

int df_gvs_audio_jitter_tick(struct df_gvs_audio_jitter *jitter, uint64_t now_ms)
{
    if (jitter == NULL || jitter->emit == NULL) return -1;
    if (now_ms < jitter->last_timestamp_ms) return -1;
    jitter->last_timestamp_ms = now_ms;
    return drain(jitter, now_ms, false);
}
