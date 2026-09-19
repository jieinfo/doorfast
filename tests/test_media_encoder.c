#include "media_encoder.h"
#include "media_credentials.h"
#include "media_frame_queue.h"
#include "test.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct df_media_encoder_config test_config(const char *program)
{
    struct df_media_encoder_config config = {
        .program = program,
        .host = "ha.local",
        .port = 8554,
        .stream = "doorfast_preview",
        .username = "doorfast",
        .fps = 10,
        .bitrate_kbps = 800,
        .encoder = DF_MEDIA_ENCODER_SOFTWARE,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    return config;
}

static int capture_encoder_arguments(struct df_media_encoder_config *config,
                                     const struct df_media_credentials *credentials,
                                     char *contents, size_t contents_size)
{
    struct df_media_encoder_process encoder = {0};
    char path[] = "/tmp/doorfast-media-argv-XXXXXX";
    ssize_t length;
    int descriptor = mkstemp(path);
    int result = DF_ERR_IO;

    if (descriptor < 0 || close(descriptor) != 0) return DF_ERR_IO;
    if (setenv("DF_TEST_MEDIA_ENCODER_ARGV", path, 1) != 0) goto cleanup;
    result = df_media_encoder_start(&encoder, config, credentials, 21);
    (void)unsetenv("DF_TEST_MEDIA_ENCODER_ARGV");
    if (result != DF_OK) goto cleanup;
    result = df_media_encoder_stop(&encoder, 100);
    if (result != DF_OK) goto cleanup;
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        result = DF_ERR_IO;
        goto cleanup;
    }
    length = read(descriptor, contents, contents_size - 1U);
    (void)close(descriptor);
    if (length <= 0) {
        result = DF_ERR_IO;
        goto cleanup;
    }
    contents[length] = '\0';
    result = DF_OK;

cleanup:
    (void)unsetenv("DF_TEST_MEDIA_ENCODER_ARGV");
    (void)unlink(path);
    return result;
}

void test_media_encoder_uses_exec_argv_and_redacts_credentials(void)
{
    struct df_media_encoder_config config = test_config("/bin/cat");
    const struct df_media_credentials credentials = {
        .rtsp_password = "secret",
    };
    struct df_media_encoder_process encoder = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(
        &encoder, &config, &credentials, 19));
    TEST_ASSERT_INT_EQ(1, encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, (fcntl(encoder.input_fd, F_GETFL) & O_NONBLOCK) != 0);
    TEST_ASSERT_INT_EQ(0, strstr(encoder.last_error, "secret") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_tick(&encoder, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
}

void test_media_encoder_rejects_stale_frame_and_restarts_on_dimension_change(void)
{
    struct df_media_encoder_config config = test_config("/bin/cat");
    struct df_media_encoder_process encoder = {0};
    struct df_media_frame frame = {
        .data = (const uint8_t[]){0xff, 0xd8, 0xff, 0xd9},
        .length = 4,
        .generation = 19,
        .timestamp_ms = 100,
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(
        &encoder, &config, NULL, 19));
    frame.generation = 18;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_encoder_write_frame(
        &encoder, &frame));
    frame.generation = 19;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_write_frame(&encoder, &frame));
    TEST_ASSERT_INT_EQ(1, (int)encoder.frames_written);
    TEST_ASSERT_INT_EQ(1, df_media_encoder_requires_restart(&encoder, 360, 480) ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_media_encoder_requires_restart(&encoder, 480, 640) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
}

void test_media_encoder_retries_backpressure_and_invalidates_broken_pipe(void)
{
    struct df_media_encoder_process encoder = {
        .input_fd = -1,
        .generation = 19,
        .running = true,
        .input_owned = true,
    };
    struct df_media_frame frame = {
        .data = (const uint8_t[]){0xff, 0xd8, 0xff, 0xd9},
        .length = 4,
        .generation = 19,
        .timestamp_ms = 100,
    };
    uint8_t fill[4096] = {0};
    int descriptors[2];
    int flags;

    TEST_ASSERT_INT_EQ(0, pipe(descriptors));
    flags = fcntl(descriptors[1], F_GETFL);
    TEST_ASSERT_INT_EQ(0, fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK));
    while (write(descriptors[1], fill, sizeof(fill)) > 0) {}
    encoder.input_fd = descriptors[1];
    TEST_ASSERT_INT_EQ(DF_MEDIA_ENCODER_RETRY,
        df_media_encoder_write_frame(&encoder, &frame));
    TEST_ASSERT_INT_EQ(1, encoder.pending_frame != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, close(descriptors[0]));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_encoder_write_frame(&encoder, &frame));
    TEST_ASSERT_INT_EQ(1, encoder.encoder_exited ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, (int)encoder.generation);
    TEST_ASSERT_INT_EQ(0, strstr(encoder.last_error, "secret") != NULL ? 1 : 0);
}

void test_media_encoder_broken_pipe_completes_when_sigpipe_is_inherited_ignored(void)
{
    pid_t child = fork();
    unsigned waited_ms = 0U;
    int status = 0;
    pid_t result = 0;

    TEST_ASSERT_INT_EQ(1, child >= 0 ? 1 : 0);
    if (child < 0) return;
    if (child == 0) {
        struct df_media_encoder_process encoder = {
            .input_fd = -1,
            .generation = 19,
            .running = true,
            .input_owned = true,
        };
        struct df_media_frame frame = {
            .data = (const uint8_t[]){0xff, 0xd8, 0xff, 0xd9},
            .length = 4,
            .generation = 19,
            .timestamp_ms = 100,
        };
        int descriptors[2];
        int write_result;

        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR || pipe(descriptors) != 0)
            _exit(2);
        (void)close(descriptors[0]);
        encoder.input_fd = descriptors[1];
        write_result = df_media_encoder_write_frame(&encoder, &frame);
        _exit(write_result == DF_ERR_IO && encoder.encoder_exited &&
              encoder.generation == 0U ? 0 : 3);
    }
    while (waited_ms < 200U) {
        struct timespec delay = {0, 1000000L};
        result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        (void)nanosleep(&delay, NULL);
        waited_ms++;
    }
    if (result == 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, &status, 0);
    }
    TEST_ASSERT_INT_EQ(1, result == child ? 1 : 0);
    if (result == child)
        TEST_ASSERT_INT_EQ(1, WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 1 : 0);
}

void test_media_encoder_stop_kills_and_reaps_unresponsive_child(void)
{
    struct df_media_encoder_process encoder = {
        .input_fd = -1,
        .generation = 19,
        .running = true,
    };
    int ready[2];
    pid_t child;
    char marker;

    TEST_ASSERT_INT_EQ(0, pipe(ready));
    child = fork();

    TEST_ASSERT_INT_EQ(1, child >= 0 ? 1 : 0);
    if (child < 0) return;
    if (child == 0) {
        (void)close(ready[0]);
        (void)signal(SIGTERM, SIG_IGN);
        (void)write(ready[1], "r", 1U);
        (void)close(ready[1]);
        for (;;) pause();
    }
    (void)close(ready[1]);
    TEST_ASSERT_INT_EQ(1, (int)read(ready[0], &marker, 1U));
    TEST_ASSERT_INT_EQ(0, close(ready[0]));
    encoder.pid = child;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 10));
    TEST_ASSERT_INT_EQ(0, (int)encoder.pid);
    TEST_ASSERT_INT_EQ(-1, (int)waitpid(child, NULL, WNOHANG));
}

void test_media_encoder_exec_uses_complete_rtsp_arguments(void)
{
    struct df_media_encoder_config config = test_config("./build/doorfast-tests");
    const struct df_media_credentials credentials = {
        .rtsp_password = "secret value",
    };
    char contents[4096];

    TEST_ASSERT_INT_EQ(DF_OK, capture_encoder_arguments(
        &config, &credentials, contents, sizeof(contents)));
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-c:v\nlibx264\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-preset\nveryfast\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-tune\nzerolatency\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-b:v\n800k\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-maxrate\n1200k\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-bufsize\n2400k\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents,
        "-x264-params\nrepeat-headers=1:scenecut=0\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-rtsp_transport\ntcp\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-rtsp_flags\nsend_bye\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents,
        "rtsp://doorfast:secret%20value@ha.local:8554/doorfast_preview\n") != NULL ? 1 : 0);
}

void test_media_encoder_requires_resolved_encoder_and_builds_hardware_argv(void)
{
    struct df_media_encoder_config config = test_config("./build/doorfast-tests");
    struct df_media_encoder_process encoder = {0};
    char contents[4096];

    config.encoder = DF_MEDIA_ENCODER_AUTO;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_encoder_start(
        &encoder, &config, NULL, 41));
    config.encoder = DF_MEDIA_ENCODER_QSV;
    TEST_ASSERT_INT_EQ(DF_OK, capture_encoder_arguments(
        &config, NULL, contents, sizeof(contents)));
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-c:v\nh264_qsv\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-look_ahead\n0\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-repeat_pps\n1\n") != NULL ? 1 : 0);
    config.encoder = DF_MEDIA_ENCODER_VAAPI;
    TEST_ASSERT_INT_EQ(DF_OK, capture_encoder_arguments(
        &config, NULL, contents, sizeof(contents)));
    TEST_ASSERT_INT_EQ(1, strstr(contents,
        "-vaapi_device\n/dev/dri/renderD128\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents,
        "-vf\nformat=nv12,hwupload\n") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "-c:v\nh264_vaapi\n") != NULL ? 1 : 0);
}

void test_media_encoder_tracks_source_and_fixed_output_dimensions_separately(void)
{
    struct df_media_encoder_config config = test_config("/bin/cat");
    struct df_media_encoder_process encoder = {0};

    config.width = 800;
    config.height = 600;
    config.resolution = DF_MEDIA_RESOLUTION_360X480;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(&encoder, &config, NULL, 42));
    TEST_ASSERT_INT_EQ(800, (int)encoder.source_width);
    TEST_ASSERT_INT_EQ(600, (int)encoder.source_height);
    TEST_ASSERT_INT_EQ(360, (int)encoder.output_width);
    TEST_ASSERT_INT_EQ(480, (int)encoder.output_height);
    TEST_ASSERT_INT_EQ(0,
        df_media_encoder_requires_restart(&encoder, 800, 600) ? 1 : 0);
    TEST_ASSERT_INT_EQ(1,
        df_media_encoder_requires_restart(&encoder, 640, 480) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
}

void test_media_encoder_tick_detects_child_exit_and_invalidates_generation(void)
{
    struct df_media_encoder_config config = test_config("/usr/bin/true");
    struct df_media_encoder_process encoder = {0};
    uint64_t now_ms = 0U;
    unsigned attempts;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(&encoder, &config, NULL, 23));
    for (attempts = 0U; attempts < 50U && !encoder.encoder_exited; attempts++) {
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
        now_ms++;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_tick(&encoder, now_ms));
    }
    TEST_ASSERT_INT_EQ(1, encoder.encoder_exited ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, (int)encoder.generation);
    TEST_ASSERT_INT_EQ(0, strstr(encoder.last_error, "secret") != NULL ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 10));
}

void test_media_encoder_preserves_documented_target_bitrate_range(void)
{
    struct df_media_encoder_config config = test_config("/usr/bin/true");
    struct df_media_encoder_process encoder = {0};
    int result;

    config.bitrate_kbps = 256;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(&encoder, &config, NULL, 31));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 10));
    config.bitrate_kbps = 2000;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(&encoder, &config, NULL, 32));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 10));
    config.bitrate_kbps = 2001;
    result = df_media_encoder_start(&encoder, &config, NULL, 33);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, result);
    if (result == DF_OK) (void)df_media_encoder_stop(&encoder, 10);
}
