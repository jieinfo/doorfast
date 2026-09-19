#ifndef DOORFAST_MEDIA_ENCODER_H
#define DOORFAST_MEDIA_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "config.h"
#include "media_credentials.h"
#include "media_frame_queue.h"

#define DF_MEDIA_ENCODER_ERROR_MAX 96U
#define DF_MEDIA_ENCODER_RETRY 3

struct df_media_encoder_config {
    const char *program;
    const char *host;
    uint16_t port;
    const char *stream;
    const char *username;
    uint8_t fps;
    uint16_t bitrate_kbps;
    enum df_media_encoder encoder;
    enum df_media_resolution resolution;
    enum df_media_profile profile;
    uint16_t width;
    uint16_t height;
};

struct df_media_encoder_process {
    pid_t pid;
    int input_fd;
    uint64_t generation;
    uint64_t last_tick_ms;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t output_width;
    uint16_t output_height;
    enum df_media_encoder encoder;
    bool running;
    bool input_owned;
    bool encoder_exited;
    uint8_t *pending_frame;
    size_t pending_length;
    size_t pending_offset;
    uint64_t pending_generation;
    uint64_t pending_timestamp_ms;
    uint64_t frames_written;
    char last_error[DF_MEDIA_ENCODER_ERROR_MAX];
};

int df_media_encoder_start(struct df_media_encoder_process *,
                           const struct df_media_encoder_config *,
                           const struct df_media_credentials *,
                           uint64_t generation);
int df_media_encoder_write_frame(struct df_media_encoder_process *,
                                 const struct df_media_frame *);
int df_media_encoder_tick(struct df_media_encoder_process *, uint64_t now_ms);
int df_media_encoder_stop(struct df_media_encoder_process *, unsigned timeout_ms);
bool df_media_encoder_requires_restart(const struct df_media_encoder_process *,
                                       uint16_t width, uint16_t height);
bool df_media_encoder_is_running(const struct df_media_encoder_process *);

#endif
