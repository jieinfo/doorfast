#ifndef DOORFAST_GVS_MEDIA_RECEIVER_H
#define DOORFAST_GVS_MEDIA_RECEIVER_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_GVS_AUDIO_PORT 8302U
#define DF_GVS_VIDEO_PORT 8303U
#define DF_GVS_MEDIA_DATAGRAM_CAPACITY 2048U
#define DF_GVS_MEDIA_RECEIVE_BUFFER_BYTES (4U * 1024U * 1024U)

enum df_gvs_media_channel {
    DF_GVS_MEDIA_AUDIO = 1,
    DF_GVS_MEDIA_VIDEO,
};

enum df_gvs_media_receiver_result {
    DF_GVS_MEDIA_RECEIVER_ERROR = -1,
    DF_GVS_MEDIA_RECEIVER_EMPTY = 0,
    DF_GVS_MEDIA_RECEIVER_DATAGRAM = 1,
    DF_GVS_MEDIA_RECEIVER_DROPPED = 2,
};

struct df_gvs_media_datagram {
    enum df_gvs_media_channel channel;
    uint32_t source_ipv4;
    size_t length;
};

struct df_gvs_media_receiver {
    int audio_fd;
    int video_fd;
    uint16_t audio_port;
    uint16_t video_port;
    unsigned receive_buffer_bytes;
    unsigned next_channel;
    uint64_t audio_received;
    uint64_t video_received;
    uint64_t truncated;
    uint64_t receive_errors;
};

int df_gvs_media_receiver_open(struct df_gvs_media_receiver *, const char *,
    uint16_t audio_port, uint16_t video_port);
int df_gvs_media_receiver_next(struct df_gvs_media_receiver *, uint8_t *,
    size_t, struct df_gvs_media_datagram *);
void df_gvs_media_receiver_close(struct df_gvs_media_receiver *);

#endif
