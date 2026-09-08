#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gvs_sync_state.h"
#include "test.h"

void test_gvs_sync_state_round_trips_uci_atomically(void) {
    char path[] = "/tmp/doorfast-sync-state-XXXXXX";
    struct stat info;
    uint16_t version = 99;
    int descriptor = mkstemp(path);

    TEST_ASSERT_INT_EQ(1, descriptor >= 0);
    if (descriptor < 0) {
        return;
    }
    (void)close(descriptor);
    TEST_ASSERT_INT_EQ(0, unlink(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_state_load(path, &version));
    TEST_ASSERT_INT_EQ(0, version);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_state_save(path, 60000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_state_load(path, &version));
    TEST_ASSERT_INT_EQ(60000, version);
    TEST_ASSERT_INT_EQ(0, stat(path, &info));
    TEST_ASSERT_INT_EQ(0600, info.st_mode & 0777);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_state_save(path, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_state_load(path, &version));
    TEST_ASSERT_INT_EQ(1, version);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_sync_state_save(path, 60001));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_sync_state_load("relative", &version));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}
