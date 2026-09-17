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
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    return config;
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
    struct df_media_encoder_process encoder = {0};
    char path[] = "/tmp/doorfast-media-argv-XXXXXX";
    char contents[4096];
    ssize_t length;
    int descriptor = mkstemp(path);

    TEST_ASSERT_INT_EQ(1, descriptor >= 0 ? 1 : 0);
    if (descriptor < 0) return;
    TEST_ASSERT_INT_EQ(0, close(descriptor));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_ENCODER_ARGV", path, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(
        &encoder, &config, &credentials, 21));
    TEST_ASSERT_INT_EQ(0, unsetenv("DF_TEST_MEDIA_ENCODER_ARGV"));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
    descriptor = open(path, O_RDONLY);
    TEST_ASSERT_INT_EQ(1, descriptor >= 0 ? 1 : 0);
    if (descriptor >= 0) {
        length = read(descriptor, contents, sizeof(contents) - 1U);
        TEST_ASSERT_INT_EQ(1, length > 0 ? 1 : 0);
        if (length > 0) {
            contents[length] = '\0';
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-c:v\nlibx264\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-preset\nveryfast\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-tune\nzerolatency\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-maxrate\n800k\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-bufsize\n1600k\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents,
                "-x264-params\nrepeat-headers=1:scenecut=0\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-rtsp_transport\ntcp\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents, "-rtsp_flags\nsend_bye\n") != NULL ? 1 : 0);
            TEST_ASSERT_INT_EQ(1, strstr(contents,
                "rtsp://doorfast:secret%20value@ha.local:8554/doorfast_preview\n") != NULL ? 1 : 0);
        }
        TEST_ASSERT_INT_EQ(0, close(descriptor));
    }
    TEST_ASSERT_INT_EQ(0, unlink(path));
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
