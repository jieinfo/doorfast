#ifndef DOORFAST_GVS_MEDIA_H
#define DOORFAST_GVS_MEDIA_H
#include <stddef.h>
#include <stdint.h>
#define DF_GVS_MEDIA_MAGIC_LEN 10U
#define DF_GVS_AUDIO_HEADER_LEN 0x2aU
#define DF_GVS_VIDEO_HEADER_LEN 0x26U
#define DF_GVS_AUDIO_MAX_PAYLOAD (1500U - DF_GVS_AUDIO_HEADER_LEN)
struct df_gvs_audio_packet { uint8_t destination[6], source[6]; uint16_t sequence; uint32_t field_c; uint16_t field_d, field_e, field_f; const uint8_t *payload; size_t payload_length; };
struct df_gvs_video_packet { uint8_t destination[6], source[6]; uint16_t frame_no, chunk_count, chunk_index, chunk_length, capacity; uint32_t full_length; const uint8_t *payload; };
int df_gvs_parse_audio(const uint8_t *, size_t, struct df_gvs_audio_packet *);
int df_gvs_parse_video(const uint8_t *, size_t, struct df_gvs_video_packet *);
int df_gvs_parse_video_datagram(const uint8_t *, size_t,
    struct df_gvs_video_packet *, size_t *consumed);
int df_gvs_serialize_audio(const uint8_t [6], const uint8_t [6], uint16_t,
    const uint8_t *, size_t, uint8_t *, size_t, size_t *);
#endif
