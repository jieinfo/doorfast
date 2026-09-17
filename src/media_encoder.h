#ifndef DOORFAST_MEDIA_ENCODER_H
#define DOORFAST_MEDIA_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "config.h"

#define DF_MEDIA_ENCODER_URL_MAX 256U
#define DF_MEDIA_ENCODER_ARGV_MAX 40U

struct df_media_encoder_options {
    const char *program;
    const char *host;
    uint16_t port;
    const char *stream;
    const char *username;
    const char *password;
    uint8_t fps;
    uint16_t bitrate_kbps;
    enum df_media_resolution resolution;
    enum df_media_profile profile;
};

struct df_media_encoder_process {
    pid_t pid;
    int input_fd;
    uint64_t generation;
    bool running;
};

int df_media_encoder_build_argv(const struct df_media_encoder_options *,
                                char *url, size_t url_size,
                                char *argv[], size_t argv_capacity);
int df_media_encoder_start(struct df_media_encoder_process *,
                           const struct df_media_encoder_options *,
                           uint64_t generation);
int df_media_encoder_write(struct df_media_encoder_process *, const uint8_t *, size_t,
                           uint64_t generation);
int df_media_encoder_stop(struct df_media_encoder_process *, unsigned timeout_ms);
bool df_media_encoder_is_running(const struct df_media_encoder_process *);

#endif
