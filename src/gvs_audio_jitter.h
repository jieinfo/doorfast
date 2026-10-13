#ifndef DOORFAST_GVS_AUDIO_JITTER_H
#define DOORFAST_GVS_AUDIO_JITTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_GVS_AUDIO_JITTER_SLOTS 4U
#define DF_GVS_AUDIO_JITTER_MAX_SAMPLES 256U
#define DF_GVS_AUDIO_JITTER_MAX_SILENCE 320U
#define DF_GVS_AUDIO_JITTER_GAP_MS 40U

struct df_gvs_audio_emission {
    const uint8_t *samples;
    size_t count;
    uint16_t sequence;
    uint64_t generation;
    uint64_t timestamp_ms;
    bool synthetic;
    bool discontinuity;
};

/* Called synchronously; samples are valid only during the callback. */
typedef int (*df_gvs_audio_jitter_emit_fn)(
    const struct df_gvs_audio_emission *, void *);

struct df_gvs_audio_jitter_slot {
    uint8_t samples[DF_GVS_AUDIO_JITTER_MAX_SAMPLES];
    size_t count;
    uint16_t sequence;
    uint64_t arrival_ms;
    bool occupied;
};

struct df_gvs_audio_jitter {
    struct df_gvs_audio_jitter_slot slots[DF_GVS_AUDIO_JITTER_SLOTS];
    uint64_t generation;
    uint16_t expected_sequence;
    bool has_sequence;
    bool auto_bind_allowed;
    uint64_t gap_deadline_ms;
    size_t last_real_samples;
    uint64_t last_timestamp_ms;
    uint64_t duplicate_packets;
    uint64_t late_packets;
    uint64_t reordered_packets;
    size_t gap_synthetic_samples;
    df_gvs_audio_jitter_emit_fn emit;
    void *context;
};

/* 0: success, 1: nonfatal rejection, negative: invalid input or callback error.
 * A callback error retains queued audio; tick retries it. A full-window push
 * that fails to free a slot has not accepted the new packet. Expected input at
 * a full window emits directly: if its callback fails, it remains caller-owned
 * and must be pushed again (tick retries only the original four queued packets).
 * If that direct callback succeeds but a queued callback fails, the input has
 * already been emitted and only queued audio needs a tick retry; expected_sequence
 * reflects this progress. Reset changes a bound generation and preserves the
 * callback/context. init allows first-packet binding; reset(0) closes until an
 * explicit nonzero reset.
 */
int df_gvs_audio_jitter_init(struct df_gvs_audio_jitter *,
    df_gvs_audio_jitter_emit_fn, void *);
void df_gvs_audio_jitter_reset(struct df_gvs_audio_jitter *, uint64_t);
int df_gvs_audio_jitter_push(struct df_gvs_audio_jitter *,
    const uint8_t *, size_t, uint16_t, uint64_t, uint64_t);
int df_gvs_audio_jitter_tick(struct df_gvs_audio_jitter *, uint64_t);

#endif
