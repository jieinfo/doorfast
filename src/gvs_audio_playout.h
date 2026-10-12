#ifndef DOORFAST_GVS_AUDIO_PLAYOUT_H
#define DOORFAST_GVS_AUDIO_PLAYOUT_H

#include "gvs_audio_buffer.h"
#include "gvs_audio_jitter.h"

struct df_gvs_audio_chunk {
    uint64_t generation, previous_revision, revision, timestamp_ms;
    size_t sample_count;
    uint64_t dropped_samples;
    bool discontinuity;
};

/* Synchronous and non-reentrant: neither callback nor context may modify the
 * buffer/playout. Both pointers expire on return. Zero commits the snapshot;
 * any nonzero result preserves pending audio for a retry at least 100 ms later. */
typedef int (*df_gvs_audio_publish_fn)(const uint8_t *,
    const struct df_gvs_audio_chunk *, void *);

struct df_gvs_audio_playout {
    struct df_gvs_audio_buffer *buffer;
    df_gvs_audio_publish_fn publish;
    void *context;
    uint64_t generation;
    uint64_t oldest_pending_ms;
    uint64_t next_retry_ms;
    bool has_pending;
    bool discontinuity;
    bool auto_bind_allowed;
};

/* Initialize with an initialized, empty buffer. init permits first-emission
 * binding if buffer init did; reset(0) closes until explicit reset(nonzero).
 * push copies emission samples immediately (it never retains their pointer).
 * push: 0 accepted, 1 nonfatal duplicate/late/generation rejection, -1 invalid.
 * tick: 0 idle/success, callback error unchanged, -1 invalid commit/clock state. */
int df_gvs_audio_playout_init(struct df_gvs_audio_playout *,
    struct df_gvs_audio_buffer *, df_gvs_audio_publish_fn, void *);
void df_gvs_audio_playout_reset(struct df_gvs_audio_playout *, uint64_t);
int df_gvs_audio_playout_push(struct df_gvs_audio_playout *,
    const struct df_gvs_audio_emission *);
int df_gvs_audio_playout_tick(struct df_gvs_audio_playout *, uint64_t);

#endif
