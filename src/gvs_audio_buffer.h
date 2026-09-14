#ifndef DOORFAST_GVS_AUDIO_BUFFER_H
#define DOORFAST_GVS_AUDIO_BUFFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DF_GVS_AUDIO_BUFFER_CAPACITY 65536U
struct df_gvs_audio_buffer { uint8_t data[DF_GVS_AUDIO_BUFFER_CAPACITY]; size_t read_offset, write_offset, length; uint64_t generation, packet_count, byte_count, sequence_gaps, last_timestamp_ms; uint16_t last_sequence; bool has_sequence; };
struct df_gvs_audio_status { uint64_t generation, packet_count, byte_count, sequence_gaps, last_timestamp_ms; size_t buffered_bytes; bool ready; };
void df_gvs_audio_buffer_init(struct df_gvs_audio_buffer *);
void df_gvs_audio_buffer_reset(struct df_gvs_audio_buffer *, uint64_t);
int df_gvs_audio_buffer_push(struct df_gvs_audio_buffer *, const uint8_t *, size_t, uint16_t, uint64_t, uint64_t);
int df_gvs_audio_buffer_read(struct df_gvs_audio_buffer *, uint8_t *, size_t, size_t *);
int df_gvs_audio_buffer_copy(const struct df_gvs_audio_buffer *, uint8_t *, size_t, size_t *);
int df_gvs_audio_buffer_status(const struct df_gvs_audio_buffer *, struct df_gvs_audio_status *);
#endif
