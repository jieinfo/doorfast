#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "media_credentials.h"
#include "test.h"

void test_media_credentials_preserve_blank_fields_and_never_echo_values(void) {
    char path[] = "/tmp/doorfast-media-credentials-XXXXXX";
    struct df_media_credentials initial = {
        .rtsp_password = "secret-a",
        .relay_token = "secret-b",
    };
    struct df_media_credentials_update update = {
        .set_rtsp_password = true,
        .rtsp_password = "secret-c",
        .set_relay_token = true,
        .relay_token = "",
    };
    struct df_media_credentials_update clear = {
        .clear_relay_token = true,
    };
    struct df_media_credentials loaded;
    struct df_media_credentials_status status;
    struct stat info;
    int descriptor = mkstemp(path);

    TEST_ASSERT_INT_EQ(1, descriptor >= 0);
    if (descriptor < 0) {
        return;
    }
    TEST_ASSERT_INT_EQ(0, close(descriptor));
    TEST_ASSERT_INT_EQ(0, unlink(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_write(path, &initial, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_write(path, NULL, &update));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_load(path, &loaded));
    TEST_ASSERT_INT_EQ(0, strcmp("secret-c", loaded.rtsp_password));
    TEST_ASSERT_INT_EQ(0, strcmp("secret-b", loaded.relay_token));
    df_media_credentials_status(&loaded, &status);
    TEST_ASSERT_INT_EQ(1, status.rtsp_password_set);
    TEST_ASSERT_INT_EQ(1, status.relay_token_set);
    TEST_ASSERT_INT_EQ(DF_OK, stat(path, &info));
    TEST_ASSERT_INT_EQ(0600, info.st_mode & 0777);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_write(path, NULL, &clear));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_load(path, &loaded));
    TEST_ASSERT_INT_EQ(0, strcmp("secret-c", loaded.rtsp_password));
    TEST_ASSERT_INT_EQ(0, strcmp("", loaded.relay_token));
    df_media_credentials_status(&loaded, &status);
    TEST_ASSERT_INT_EQ(1, status.rtsp_password_set);
    TEST_ASSERT_INT_EQ(0, status.relay_token_set);
    TEST_ASSERT_INT_EQ(0, unlink(path));
}
