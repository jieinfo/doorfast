#ifndef DOORFAST_GVS_AUDIO_BUFFER_H
#define DOORFAST_GVS_AUDIO_BUFFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_GVS_AUDIO_BUFFER_CAPACITY 65536U

enum df_gvs_audio_buffer_result {
    DF_GVS_AUDIO_BUFFER_ERROR = -1,
    DF_GVS_AUDIO_BUFFER_ACCEPTED = 0,
    DF_GVS_AUDIO_BUFFER_DUPLICATE = 1,
    DF_GVS_AUDIO_BUFFER_LATE = 2,
    DF_GVS_AUDIO_BUFFER_STALE = 3,
};

struct df_gvs_audio_buffer {
    uint8_t data[DF_GVS_AUDIO_BUFFER_CAPACITY];
    size_t read_offset;
    size_t write_offset;
    size_t length;
    uint64_t generation;
    uint64_t packet_count;
    uint64_t byte_count;
    uint64_t sample_count; /* Real and concealed A-law samples. */
    uint64_t concealed_samples;
    uint64_t sequence_gaps;
    uint64_t missing_packets;
    uint64_t duplicate_packets;
    uint64_t late_packets;
    uint64_t last_timestamp_ms;
    uint64_t snapshot_packet_count;
    uint64_t snapshot_previous_packet_count;
    uint64_t snapshot_source_bytes;
    uint64_t snapshot_source_samples;
    uint64_t snapshot_dropped_bytes; /* Dropped output A-law sample bytes. */
    uint64_t snapshot_timestamp_ms;
    size_t snapshot_bytes;
    uint16_t last_sequence;
    bool has_sequence;
    bool snapshot_ready;
    bool auto_bind_allowed;
};

struct df_gvs_audio_status {
    uint64_t generation;
    uint64_t packet_count;
    uint64_t byte_count;
    uint64_t sample_count;
    uint64_t concealed_samples;
    uint64_t sequence_gaps;
    uint64_t missing_packets;
    uint64_t duplicate_packets;
    uint64_t late_packets;
    uint64_t last_timestamp_ms;
    uint64_t snapshot_packet_count;
    uint64_t snapshot_previous_packet_count;
    uint64_t snapshot_source_bytes;
    uint64_t snapshot_source_samples;
    uint64_t snapshot_dropped_bytes;
    uint64_t snapshot_timestamp_ms;
    size_t buffered_bytes;
    size_t snapshot_bytes;
    bool ready;
    bool snapshot_ready;
};
/* init permits first-packet binding. reset(0) clears and closes; only an
 * explicit nonzero reset reopens a closed buffer. */
void df_gvs_audio_buffer_init(struct df_gvs_audio_buffer *);
void df_gvs_audio_buffer_reset(struct df_gvs_audio_buffer *, uint64_t);
int df_gvs_audio_buffer_push(struct df_gvs_audio_buffer *, const uint8_t *,
    size_t, uint16_t, uint64_t, uint64_t);
int df_gvs_audio_buffer_push_silence(struct df_gvs_audio_buffer *, size_t,
    uint64_t);
int df_gvs_audio_buffer_read(struct df_gvs_audio_buffer *, uint8_t *, size_t,
    size_t *);
int df_gvs_audio_buffer_copy(const struct df_gvs_audio_buffer *, uint8_t *,
    size_t, size_t *);
int df_gvs_audio_buffer_copy_pending(const struct df_gvs_audio_buffer *,
    uint8_t *, size_t, size_t *);
/* copy_pending preserves the oldest available pending prefix; this helper
 * selects the latest pending samples when capacity is smaller than available. */
int df_gvs_audio_buffer_copy_pending_tail(const struct df_gvs_audio_buffer *,
    uint8_t *, size_t, size_t *);
/* source_samples is the exported output sample count, including concealment;
 * commits both cursors and records source bytes from real byte_count only. */
int df_gvs_audio_buffer_mark_snapshot(struct df_gvs_audio_buffer *, uint64_t,
    size_t, uint64_t);
int df_gvs_audio_buffer_status(const struct df_gvs_audio_buffer *,
    struct df_gvs_audio_status *);
#endif
