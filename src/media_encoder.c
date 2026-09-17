#include "media_encoder.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "doorfast.h"

static const char *df_profile_name(enum df_media_profile profile)
{
    return profile == DF_MEDIA_PROFILE_MAIN ? "main" : "baseline";
}

static bool df_encoder_text_valid(const char *value, size_t max)
{
    size_t i;
    if (value == NULL || value[0] == '\0' || strlen(value) > max) return false;
    for (i = 0; value[i] != '\0'; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c <= 0x20U || c == '\'' || c == '"' || c == '\\' || c == '@' ||
            c == '/' || c == '?' || c == '#') return false;
    }
    return true;
}

int df_media_encoder_build_argv(const struct df_media_encoder_options *options,
                                char *url, size_t url_size,
                                char *argv[], size_t argv_capacity)
{
    const char *program;
    static _Thread_local char fps_text[4];
    static _Thread_local char bitrate_text[16];
    static _Thread_local char gop_text[4];
    int count;

    if (options == NULL || url == NULL || url_size == 0U || argv == NULL ||
        argv_capacity < 33U || options->port == 0U || options->fps == 0U ||
        options->fps > 60U || options->bitrate_kbps == 0U ||
        !df_encoder_text_valid(options->host, 63U) ||
        !df_encoder_text_valid(options->stream, 64U)) return DF_ERR_INVALID;
    if (options->username != NULL && options->username[0] != '\0' &&
        !df_encoder_text_valid(options->username, 32U)) return DF_ERR_INVALID;
    if (options->password != NULL && strlen(options->password) > 128U) return DF_ERR_INVALID;
    if (options->resolution < DF_MEDIA_RESOLUTION_SOURCE ||
        options->resolution > DF_MEDIA_RESOLUTION_240X320 ||
        options->profile < DF_MEDIA_PROFILE_BASELINE ||
        options->profile > DF_MEDIA_PROFILE_MAIN) return DF_ERR_INVALID;

    if (options->username != NULL && options->username[0] != '\0') {
        if (options->password == NULL || options->password[0] == '\0' ||
            strchr(options->password, '@') != NULL || strchr(options->password, '/') != NULL)
            return DF_ERR_INVALID;
        if (snprintf(url, url_size, "rtsp://%s:%s@%s:%u/%s", options->username,
                     options->password, options->host, (unsigned)options->port,
                     options->stream) >= (int)url_size) return DF_ERR_INVALID;
    } else if (snprintf(url, url_size, "rtsp://%s:%u/%s", options->host,
                        (unsigned)options->port, options->stream) >= (int)url_size) {
        return DF_ERR_INVALID;
    }
    program = options->program != NULL && options->program[0] != '\0' ? options->program : "ffmpeg";
    (void)snprintf(fps_text, sizeof(fps_text), "%u", (unsigned)options->fps);
    (void)snprintf(bitrate_text, sizeof(bitrate_text), "%uk", (unsigned)options->bitrate_kbps);
    (void)snprintf(gop_text, sizeof(gop_text), "%u", (unsigned)options->fps);
    argv[0] = (char *)program;
    argv[1] = (char *)"-hide_banner";
    argv[2] = (char *)"-loglevel";
    argv[3] = (char *)"error";
    argv[4] = (char *)"-f";
    argv[5] = (char *)"image2pipe";
    argv[6] = (char *)"-framerate";
    argv[7] = fps_text;
    argv[8] = (char *)"-vcodec";
    argv[9] = (char *)"mjpeg";
    argv[10] = (char *)"-i";
    argv[11] = (char *)"pipe:0";
    argv[12] = (char *)"-an";
    argv[13] = (char *)"-c:v";
    argv[14] = (char *)"libx264";
    argv[15] = (char *)"-profile:v";
    argv[16] = (char *)df_profile_name(options->profile);
    argv[17] = (char *)"-pix_fmt";
    argv[18] = (char *)"yuv420p";
    argv[19] = (char *)"-bf";
    argv[20] = (char *)"0";
    argv[21] = (char *)"-g";
    argv[22] = gop_text;
    argv[23] = (char *)"-keyint_min";
    argv[24] = gop_text;
    argv[25] = (char *)"-b:v";
    argv[26] = bitrate_text;
    argv[27] = (char *)"-rtsp_transport";
    argv[28] = (char *)"tcp";
    argv[29] = (char *)"-f";
    argv[30] = (char *)"rtsp";
    argv[31] = url;
    count = 32;
    argv[count] = NULL;
    return DF_OK;
}

int df_media_encoder_start(struct df_media_encoder_process *encoder,
                           const struct df_media_encoder_options *options,
                           uint64_t generation)
{
    int pipe_fds[2];
    char url[DF_MEDIA_ENCODER_URL_MAX];
    char *argv[DF_MEDIA_ENCODER_ARGV_MAX];
    pid_t pid;

    if (encoder == NULL || generation == 0U || encoder->running ||
        df_media_encoder_build_argv(options, url, sizeof(url), argv,
                                    DF_MEDIA_ENCODER_ARGV_MAX) != DF_OK) return DF_ERR_INVALID;
    if (pipe(pipe_fds) != 0) return DF_ERR_IO;
    (void)fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);
    pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]); close(pipe_fds[1]); return DF_ERR_IO;
    }
    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        (void)dup2(pipe_fds[0], STDIN_FILENO);
        close(pipe_fds[0]); close(pipe_fds[1]);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        (void)execvp(argv[0], argv);
        _exit(127);
    }
    close(pipe_fds[0]);
    encoder->pid = pid;
    encoder->input_fd = pipe_fds[1];
    encoder->generation = generation;
    encoder->running = true;
    return DF_OK;
}

int df_media_encoder_write(struct df_media_encoder_process *encoder, const uint8_t *data,
                           size_t length, uint64_t generation)
{
    size_t offset = 0U;
    ssize_t written;

    if (encoder == NULL || data == NULL || length == 0U || !encoder->running ||
        generation == 0U || encoder->generation != generation) return DF_ERR_INVALID;
    while (offset < length) {
        written = write(encoder->input_fd, data + offset, length - offset);
        if (written > 0) offset += (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else return DF_ERR_IO;
    }
    return DF_OK;
}

int df_media_encoder_stop(struct df_media_encoder_process *encoder, unsigned timeout_ms)
{
    unsigned waited = 0U;
    int status;
    pid_t result;

    if (encoder == NULL) return DF_ERR_INVALID;
    if (!encoder->running) return DF_OK;
    close(encoder->input_fd);
    encoder->input_fd = -1;
    while (waited <= timeout_ms) {
        result = waitpid(encoder->pid, &status, WNOHANG);
        if (result == encoder->pid) {
            encoder->running = false; encoder->pid = 0; encoder->generation = 0;
            return DF_OK;
        }
        if (result < 0 && errno != EINTR) break;
        if (result == 0) {
            struct timespec delay = {0, 10000000L};
            nanosleep(&delay, NULL); waited += 10U;
        }
    }
    (void)kill(encoder->pid, SIGTERM);
    do { result = waitpid(encoder->pid, &status, 0); } while (result < 0 && errno == EINTR);
    encoder->running = false; encoder->pid = 0; encoder->generation = 0;
    return result < 0 ? DF_ERR_IO : DF_OK;
}

bool df_media_encoder_is_running(const struct df_media_encoder_process *encoder)
{
    return encoder != NULL && encoder->running;
}
