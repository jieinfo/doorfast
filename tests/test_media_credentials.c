#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "media_credentials.h"
#include "test.h"

void test_media_credentials_preserve_blank_rtsp_password_and_write_only_rtsp(void) {
    char path[] = "/tmp/doorfast-media-credentials-XXXXXX";
    struct df_media_credentials initial = {
        .rtsp_password = "secret-a",
    };
    struct df_media_credentials_update update = {
        .set_rtsp_password = true,
        .rtsp_password = "secret-c",
    };
    struct df_media_credentials_update clear = {
        .clear_rtsp_password = true,
    };
    struct df_media_credentials loaded;
    struct df_media_credentials_status status;
    struct stat info;
    char contents[64] = {0};
    ssize_t length;
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
    df_media_credentials_status(&loaded, &status);
    TEST_ASSERT_INT_EQ(1, status.rtsp_password_set);
    TEST_ASSERT_INT_EQ(DF_OK, stat(path, &info));
    TEST_ASSERT_INT_EQ(0600, info.st_mode & 0777);
    descriptor = open(path, O_RDONLY);
    TEST_ASSERT_INT_EQ(1, descriptor >= 0);
    length = descriptor < 0 ? -1 : read(descriptor, contents,
        sizeof(contents) - 1U);
    TEST_ASSERT_INT_EQ((int)strlen("rtsp_password=secret-c\n"), (int)length);
    if (length >= 0) contents[length] = '\0';
    TEST_ASSERT_INT_EQ(0, strcmp("rtsp_password=secret-c\n", contents));
    if (descriptor >= 0) TEST_ASSERT_INT_EQ(0, close(descriptor));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_write(path, NULL, &clear));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_load(path, &loaded));
    TEST_ASSERT_INT_EQ(0, strcmp("", loaded.rtsp_password));
    df_media_credentials_status(&loaded, &status);
    TEST_ASSERT_INT_EQ(0, status.rtsp_password_set);
    TEST_ASSERT_INT_EQ(0, unlink(path));
}
