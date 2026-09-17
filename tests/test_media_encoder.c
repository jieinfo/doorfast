#include "media_encoder.h"
#include "test.h"

#include <string.h>
#include <unistd.h>

void test_media_encoder_builds_bounded_rtsp_argv(void)
{
    const struct df_media_encoder_options options = {
        .program = "/bin/cat", .host = "ha.local", .port = 8554,
        .stream = "doorfast_preview", .username = "doorfast", .password = "secret",
        .fps = 10, .bitrate_kbps = 800, .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    char url[DF_MEDIA_ENCODER_URL_MAX];
    char *argv[DF_MEDIA_ENCODER_ARGV_MAX];

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_build_argv(
        &options, url, sizeof(url), argv, DF_MEDIA_ENCODER_ARGV_MAX));
    TEST_ASSERT_INT_EQ(0, strcmp("rtsp://doorfast:secret@ha.local:8554/doorfast_preview", url));
    TEST_ASSERT_INT_EQ(0, strcmp("10", argv[7]));
    TEST_ASSERT_INT_EQ(0, strcmp("800k", argv[26]));
    TEST_ASSERT_INT_EQ(0, strcmp("tcp", argv[28]));
    TEST_ASSERT_INT_EQ(0, strcmp(url, argv[31]));
    TEST_ASSERT_INT_EQ(0, argv[32] == NULL ? 0 : 1);
}

void test_media_encoder_generation_and_cleanup(void)
{
    const struct df_media_encoder_options options = {
        .program = "/bin/cat", .host = "127.0.0.1", .port = 8554,
        .stream = "preview", .fps = 5, .bitrate_kbps = 256,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE, .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    struct df_media_encoder_process encoder = {0};
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_start(&encoder, &options, 19));
    TEST_ASSERT_INT_EQ(1, df_media_encoder_is_running(&encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_encoder_write(&encoder, jpeg, sizeof(jpeg), 20));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_write(&encoder, jpeg, sizeof(jpeg), 19));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
    TEST_ASSERT_INT_EQ(0, df_media_encoder_is_running(&encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_stop(&encoder, 100));
}
