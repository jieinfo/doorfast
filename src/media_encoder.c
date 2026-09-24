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
#include "gvs_video_reassembly.h"

#define DF_MEDIA_ENCODER_ARGV_MAX 64U
#define DF_MEDIA_ENCODER_DESTINATION_MAX 1024U
#define DF_MEDIA_ENCODER_POLL_MS 10U

struct df_media_encoder_command {
    char destination[DF_MEDIA_ENCODER_DESTINATION_MAX];
    char fps[4];
    char bitrate[16];
    char maxrate[16];
    char buffer[16];
    char gop[4];
    char filter[96];
    char *argv[DF_MEDIA_ENCODER_ARGV_MAX];
};

static void df_media_encoder_clear(void *memory, size_t length)
{
    volatile unsigned char *cursor = memory;

    while (length > 0U) {
        *cursor++ = 0U;
        length--;
    }
}

static void df_media_encoder_error(struct df_media_encoder_process *process,
                                   const char *message)
{
    if (process == NULL) return;
    (void)snprintf(process->last_error, sizeof(process->last_error), "%s",
                   message == NULL ? "" : message);
}

static bool df_media_encoder_text_valid(const char *value, size_t maximum)
{
    size_t index;
    size_t length;

    if (value == NULL || value[0] == '\0') return false;
    length = strlen(value);
    if (length > maximum) return false;
    for (index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];

        if (character < 0x21U || character > 0x7eU || character == '@' ||
            character == '/' || character == '?' || character == '#') return false;
    }
    return true;
}

static int df_media_encoder_url_encode(const char *source, char *destination,
                                       size_t destination_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t input;
    size_t output = 0U;

    if (source == NULL || destination == NULL || destination_size == 0U)
        return DF_ERR_INVALID;
    for (input = 0U; source[input] != '\0'; input++) {
        unsigned char character = (unsigned char)source[input];
        bool unreserved = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') ||
                          character == '-' || character == '.' ||
                          character == '_' || character == '~';

        if (unreserved) {
            if (output + 1U >= destination_size) return DF_ERR_INVALID;
            destination[output++] = (char)character;
        } else {
            if (output + 3U >= destination_size) return DF_ERR_INVALID;
            destination[output++] = '%';
            destination[output++] = hex[character >> 4U];
            destination[output++] = hex[character & 0x0fU];
        }
    }
    destination[output] = '\0';
    return DF_OK;
}

static int df_media_encoder_dimensions(const struct df_media_encoder_config *config,
                                       uint16_t *source_width,
                                       uint16_t *source_height,
                                       uint16_t *output_width,
                                       uint16_t *output_height)
{
    if (config == NULL || source_width == NULL || source_height == NULL ||
        output_width == NULL || output_height == NULL) return DF_ERR_INVALID;
    *source_width = config->width == 0U ? 480U : config->width;
    *source_height = config->height == 0U ? 640U : config->height;
    switch (config->resolution) {
    case DF_MEDIA_RESOLUTION_SOURCE:
        *output_width = *source_width;
        *output_height = *source_height;
        break;
    case DF_MEDIA_RESOLUTION_480X640:
        *output_width = 480U; *output_height = 640U; break;
    case DF_MEDIA_RESOLUTION_360X480:
        *output_width = 360U; *output_height = 480U; break;
    case DF_MEDIA_RESOLUTION_240X320:
        *output_width = 240U; *output_height = 320U; break;
    default: return DF_ERR_INVALID;
    }
    if ((*source_width & 1U) != 0U || (*source_height & 1U) != 0U ||
        (*output_width & 1U) != 0U || (*output_height & 1U) != 0U)
        return DF_ERR_INVALID;
    return DF_OK;
}

static int df_media_encoder_append(struct df_media_encoder_command *command,
                                   size_t *count, char *value)
{
    if (command == NULL || count == NULL || value == NULL ||
        *count + 1U >= DF_MEDIA_ENCODER_ARGV_MAX) return DF_ERR_INVALID;
    command->argv[(*count)++] = value;
    return DF_OK;
}

static int df_media_encoder_command_build(
    const struct df_media_encoder_config *config,
    const struct df_media_credentials *credentials,
    struct df_media_encoder_command *command,
    enum df_media_encoder *selected)
{
    const char *program;
    const char *profile;
    const char *password = "";
    enum df_media_encoder encoder;
    char encoded_user[128];
    char encoded_password[DF_MEDIA_CREDENTIAL_VALUE_MAX * 3U + 1U];
    uint16_t source_width;
    uint16_t source_height;
    uint16_t output_width;
    uint16_t output_height;
    unsigned buffer_kbps;
    unsigned maxrate_kbps;
    size_t count = 0U;
    int result;

    if (config == NULL || command == NULL || selected == NULL ||
        config->port == 0U || config->fps == 0U || config->fps > 60U ||
        config->bitrate_kbps < 256U || config->bitrate_kbps > 2000U ||
        !df_media_encoder_text_valid(config->host, 63U) ||
        !df_media_encoder_text_valid(config->stream, 64U) ||
        (config->username != NULL && config->username[0] != '\0' &&
         !df_media_encoder_text_valid(config->username, 32U)) ||
        config->profile < DF_MEDIA_PROFILE_BASELINE ||
        config->profile > DF_MEDIA_PROFILE_MAIN ||
        df_media_encoder_dimensions(config, &source_width, &source_height,
                                    &output_width, &output_height) != DF_OK)
        return DF_ERR_INVALID;
    encoder = config->encoder;
    if (encoder < DF_MEDIA_ENCODER_SOFTWARE || encoder > DF_MEDIA_ENCODER_QSV)
        return DF_ERR_INVALID;
    if (credentials != NULL) {
        if (memchr(credentials->rtsp_password, '\0',
                   sizeof(credentials->rtsp_password)) == NULL)
            return DF_ERR_INVALID;
        password = credentials->rtsp_password;
    }
    memset(command, 0, sizeof(*command));
    if (password[0] != '\0') {
        if (config->username == NULL || config->username[0] == '\0' ||
            df_media_encoder_url_encode(config->username, encoded_user,
                                        sizeof(encoded_user)) != DF_OK ||
            df_media_encoder_url_encode(password, encoded_password,
                                        sizeof(encoded_password)) != DF_OK ||
            snprintf(command->destination, sizeof(command->destination),
                     "rtsp://%s:%s@%s:%u/%s", encoded_user, encoded_password,
                     config->host, (unsigned)config->port, config->stream) >=
                (int)sizeof(command->destination)) return DF_ERR_INVALID;
    } else if (snprintf(command->destination, sizeof(command->destination),
                        "rtsp://%s:%u/%s", config->host,
                        (unsigned)config->port, config->stream) >=
               (int)sizeof(command->destination)) return DF_ERR_INVALID;
    maxrate_kbps = ((unsigned)config->bitrate_kbps * 3U + 1U) / 2U;
    buffer_kbps = maxrate_kbps * 2U;
    (void)snprintf(command->fps, sizeof(command->fps), "%u", (unsigned)config->fps);
    (void)snprintf(command->gop, sizeof(command->gop), "%u", (unsigned)config->fps);
    (void)snprintf(command->bitrate, sizeof(command->bitrate), "%uk",
                   (unsigned)config->bitrate_kbps);
    (void)snprintf(command->maxrate, sizeof(command->maxrate), "%uk",
                   maxrate_kbps);
    (void)snprintf(command->buffer, sizeof(command->buffer), "%uk", buffer_kbps);
    if (encoder == DF_MEDIA_ENCODER_VAAPI) {
        if (config->resolution == DF_MEDIA_RESOLUTION_SOURCE)
            (void)snprintf(command->filter, sizeof(command->filter),
                           "format=nv12,hwupload");
        else
            (void)snprintf(command->filter, sizeof(command->filter),
                           "format=nv12,hwupload,scale_vaapi=w=%u:h=%u",
                           (unsigned)output_width, (unsigned)output_height);
    } else if (encoder == DF_MEDIA_ENCODER_QSV) {
        if (config->resolution == DF_MEDIA_RESOLUTION_SOURCE)
            (void)snprintf(command->filter, sizeof(command->filter), "format=nv12");
        else
            (void)snprintf(command->filter, sizeof(command->filter),
                           "scale=%u:%u,format=nv12", (unsigned)output_width,
                           (unsigned)output_height);
    } else if (config->resolution != DF_MEDIA_RESOLUTION_SOURCE) {
        (void)snprintf(command->filter, sizeof(command->filter), "scale=%u:%u",
                       (unsigned)output_width, (unsigned)output_height);
    }
    program = config->program != NULL && config->program[0] != '\0' ?
        config->program : "ffmpeg";
    profile = config->profile == DF_MEDIA_PROFILE_MAIN ? "main" : "baseline";

#define APPEND(value) do { result = df_media_encoder_append(command, &count, (char *)(value)); \
    if (result != DF_OK) return result; } while (0)
    APPEND(program); APPEND("-hide_banner"); APPEND("-loglevel"); APPEND("error");
    if (encoder == DF_MEDIA_ENCODER_VAAPI) {
        APPEND("-vaapi_device"); APPEND("/dev/dri/renderD128");
    }
    APPEND("-f"); APPEND("image2pipe"); APPEND("-vcodec"); APPEND("mjpeg");
    APPEND("-framerate"); APPEND(command->fps); APPEND("-probesize");
    APPEND("32"); APPEND("-i"); APPEND("pipe:0");
    APPEND("-an");
    if (command->filter[0] != '\0') {
        APPEND("-vf"); APPEND(command->filter);
    }
    APPEND("-c:v");
    if (encoder == DF_MEDIA_ENCODER_SOFTWARE) {
        APPEND("libx264"); APPEND("-preset"); APPEND("veryfast");
        APPEND("-tune"); APPEND("zerolatency");
    } else if (encoder == DF_MEDIA_ENCODER_VAAPI) {
        APPEND("h264_vaapi");
    } else {
        APPEND("h264_qsv"); APPEND("-preset"); APPEND("veryfast");
        APPEND("-look_ahead"); APPEND("0"); APPEND("-repeat_pps"); APPEND("1");
    }
    APPEND("-pix_fmt"); APPEND("yuv420p"); APPEND("-profile:v"); APPEND(profile);
    APPEND("-bf"); APPEND("0"); APPEND("-g"); APPEND(command->gop);
    APPEND("-keyint_min"); APPEND(command->gop); APPEND("-b:v");
    APPEND(command->bitrate); APPEND("-maxrate"); APPEND(command->maxrate);
    APPEND("-bufsize"); APPEND(command->buffer);
    if (encoder == DF_MEDIA_ENCODER_SOFTWARE) {
        APPEND("-x264-params"); APPEND("repeat-headers=1:scenecut=0");
    } else if (encoder == DF_MEDIA_ENCODER_VAAPI) {
        APPEND("-bsf:v"); APPEND("dump_extra=freq=keyframe");
    }
    APPEND("-f"); APPEND("rtsp"); APPEND("-rtsp_transport"); APPEND("tcp");
    APPEND(command->destination);
#undef APPEND
    command->argv[count] = NULL;
    *selected = encoder;
    return DF_OK;
}

static void df_media_encoder_pending_clear(struct df_media_encoder_process *process)
{
    if (process == NULL) return;
    free(process->pending_frame);
    process->pending_frame = NULL;
    process->pending_length = 0U;
    process->pending_offset = 0U;
    process->pending_generation = 0U;
    process->pending_timestamp_ms = 0U;
}

static void df_media_encoder_invalidate(struct df_media_encoder_process *process,
                                        bool exited)
{
    if (process == NULL) return;
    if (process->input_owned && process->input_fd >= 0)
        (void)close(process->input_fd);
    process->input_fd = -1;
    process->input_owned = false;
    process->generation = 0U;
    process->running = false;
    process->encoder_exited = exited;
    df_media_encoder_pending_clear(process);
}

static ssize_t df_media_encoder_write_no_sigpipe(int descriptor,
                                                 const void *buffer, size_t length)
{
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    bool already_pending = false;
    ssize_t result;
    int saved_errno;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (sigpending(&pending) == 0) already_pending = sigismember(&pending, SIGPIPE) == 1;
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) return -1;
    result = write(descriptor, buffer, length);
    saved_errno = errno;
    if (result < 0 && saved_errno == EPIPE && !already_pending &&
        sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
        int caught_signal;
        (void)sigwait(&blocked, &caught_signal);
    }
    (void)sigprocmask(SIG_SETMASK, &previous, NULL);
    errno = saved_errno;
    return result;
}

static int df_media_encoder_flush(struct df_media_encoder_process *process)
{
    while (process->pending_offset < process->pending_length) {
        ssize_t written = df_media_encoder_write_no_sigpipe(
            process->input_fd, process->pending_frame + process->pending_offset,
            process->pending_length - process->pending_offset);

        if (written > 0) {
            process->pending_offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return DF_MEDIA_ENCODER_RETRY;
        if (written < 0 && errno == EPIPE) {
            df_media_encoder_error(process, "encoder input closed");
            df_media_encoder_invalidate(process, true);
            return DF_ERR_IO;
        }
        df_media_encoder_error(process, "encoder input write failed");
        return DF_ERR_IO;
    }
    process->frames_written++;
    df_media_encoder_pending_clear(process);
    return DF_OK;
}

static int df_media_encoder_wait_bounded(pid_t pid, unsigned timeout_ms,
                                         bool *reaped)
{
    unsigned elapsed = 0U;

    if (reaped == NULL || pid <= 0) return DF_ERR_INVALID;
    *reaped = false;
    for (;;) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid || (result < 0 && errno == ECHILD)) {
            *reaped = true;
            return DF_OK;
        }
        if (result < 0 && errno != EINTR) return DF_ERR_IO;
        if (elapsed >= timeout_ms) return DF_OK;
        {
            unsigned slice = timeout_ms - elapsed;
            struct timespec delay;
            if (slice > DF_MEDIA_ENCODER_POLL_MS) slice = DF_MEDIA_ENCODER_POLL_MS;
            delay.tv_sec = 0;
            delay.tv_nsec = (long)slice * 1000000L;
            (void)nanosleep(&delay, NULL);
            elapsed += slice;
        }
    }
}

int df_media_encoder_start(struct df_media_encoder_process *process,
                           const struct df_media_encoder_config *config,
                           const struct df_media_credentials *credentials,
                           uint64_t generation)
{
    struct df_media_encoder_command command;
    enum df_media_encoder selected;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t output_width;
    uint16_t output_height;
    int pipe_fds[2];
    int flags;
    pid_t pid;

    if (process == NULL || config == NULL || generation == 0U ||
        process->running || process->pid > 0 || process->pending_frame != NULL ||
        df_media_encoder_command_build(config, credentials, &command, &selected) != DF_OK ||
        df_media_encoder_dimensions(config, &source_width, &source_height,
                                    &output_width, &output_height) != DF_OK)
        return DF_ERR_INVALID;
    process->input_fd = -1;
    process->encoder_exited = false;
    process->last_error[0] = '\0';
    if (pipe(pipe_fds) != 0) {
        df_media_encoder_error(process, "encoder pipe failed");
        return DF_ERR_IO;
    }
    (void)fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);
    flags = fcntl(pipe_fds[1], F_GETFL);
    if (flags < 0 || fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(pipe_fds[0]);
        (void)close(pipe_fds[1]);
        df_media_encoder_error(process, "encoder pipe setup failed");
        return DF_ERR_IO;
    }
    pid = fork();
    if (pid < 0) {
        (void)close(pipe_fds[0]);
        (void)close(pipe_fds[1]);
        df_media_encoder_clear(&command, sizeof(command));
        df_media_encoder_error(process, "encoder spawn failed");
        return DF_ERR_IO;
    }
    if (pid == 0) {
        int null_fd;

        if (pipe_fds[0] != STDIN_FILENO &&
            dup2(pipe_fds[0], STDIN_FILENO) < 0) _exit(127);
        if (pipe_fds[0] != STDIN_FILENO) (void)close(pipe_fds[0]);
        if (pipe_fds[1] != STDIN_FILENO) (void)close(pipe_fds[1]);
        null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0) _exit(127);
        if (strcmp(command.argv[0], "ffmpeg") != 0 &&
            dup2(null_fd, STDERR_FILENO) < 0) _exit(127);
        if (null_fd > STDERR_FILENO) (void)close(null_fd);
        (void)execvp(command.argv[0], command.argv);
        _exit(127);
    }
    (void)close(pipe_fds[0]);
    df_media_encoder_clear(&command, sizeof(command));
    process->pid = pid;
    process->input_fd = pipe_fds[1];
    process->input_owned = true;
    process->generation = generation;
    process->last_tick_ms = 0U;
    process->source_width = source_width;
    process->source_height = source_height;
    process->output_width = output_width;
    process->output_height = output_height;
    process->encoder = selected;
    process->running = true;
    return DF_OK;
}

int df_media_encoder_write_frame(struct df_media_encoder_process *process,
                                 const struct df_media_frame *frame)
{
    if (process == NULL || frame == NULL || frame->data == NULL ||
        frame->length == 0U || frame->length > DF_GVS_VIDEO_MAX_FRAME ||
        !process->running || process->input_fd < 0 ||
        frame->generation == 0U || frame->generation != process->generation)
        return DF_ERR_INVALID;
    if (process->pending_frame != NULL) {
        if (process->pending_generation != frame->generation ||
            process->pending_timestamp_ms != frame->timestamp_ms ||
            process->pending_length != frame->length) return DF_MEDIA_ENCODER_RETRY;
        return df_media_encoder_flush(process);
    }
    process->pending_frame = malloc(frame->length);
    if (process->pending_frame == NULL) {
        df_media_encoder_error(process, "encoder frame allocation failed");
        return DF_ERR_IO;
    }
    memcpy(process->pending_frame, frame->data, frame->length);
    process->pending_length = frame->length;
    process->pending_generation = frame->generation;
    process->pending_timestamp_ms = frame->timestamp_ms;
    return df_media_encoder_flush(process);
}

int df_media_encoder_tick(struct df_media_encoder_process *process, uint64_t now_ms)
{
    int status;
    pid_t result;

    if (process == NULL) return DF_ERR_INVALID;
    if (now_ms < process->last_tick_ms) return DF_ERR_INVALID;
    process->last_tick_ms = now_ms;
    if (process->pid <= 0) return DF_OK;
    result = waitpid(process->pid, &status, WNOHANG);
    if (result == 0) return DF_OK;
    if (result == process->pid || (result < 0 && errno == ECHILD)) {
        process->pid = 0;
        df_media_encoder_error(process, "encoder exited");
        df_media_encoder_invalidate(process, true);
        return DF_OK;
    }
    if (result < 0 && errno == EINTR) return DF_OK;
    df_media_encoder_error(process, "encoder status failed");
    return DF_ERR_IO;
}

int df_media_encoder_stop(struct df_media_encoder_process *process,
                          unsigned timeout_ms)
{
    pid_t pid;
    bool reaped = false;
    int result;

    if (process == NULL) return DF_ERR_INVALID;
    pid = process->pid;
    df_media_encoder_invalidate(process, false);
    if (pid <= 0) {
        process->pid = 0;
        return DF_OK;
    }
    result = df_media_encoder_wait_bounded(pid, timeout_ms, &reaped);
    if (result != DF_OK) goto failed;
    if (!reaped) {
        if (kill(pid, SIGTERM) != 0 && errno != ESRCH) goto failed;
        result = df_media_encoder_wait_bounded(pid, timeout_ms, &reaped);
        if (result != DF_OK) goto failed;
    }
    if (!reaped) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) goto failed;
        result = df_media_encoder_wait_bounded(pid, timeout_ms, &reaped);
        if (result != DF_OK) goto failed;
    }
    if (!reaped) {
        process->pid = pid;
        df_media_encoder_error(process, "encoder reap timeout");
        return DF_ERR_IO;
    }
    process->pid = 0;
    process->last_error[0] = '\0';
    return DF_OK;

failed:
    process->pid = pid;
    df_media_encoder_error(process, "encoder cleanup failed");
    return DF_ERR_IO;
}

bool df_media_encoder_requires_restart(const struct df_media_encoder_process *process,
                                       uint16_t width, uint16_t height)
{
    return process != NULL && process->running && process->source_width != 0U &&
           process->source_height != 0U &&
           (process->source_width != width || process->source_height != height);
}

bool df_media_encoder_is_running(const struct df_media_encoder_process *process)
{
    return process != NULL && process->running;
}
