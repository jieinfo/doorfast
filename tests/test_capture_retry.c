#include "capture_retry.h"
#include "test.h"

void test_capture_retry_is_bounded_and_resets_after_recovery(void) {
    static const unsigned expected_delays[] = {250, 500, 1000, 2000, 4000};
    struct df_capture_retry retry = {0};
    unsigned delay = 0;
    unsigned i;

    for (i = 0; i < sizeof(expected_delays) / sizeof(expected_delays[0]); ++i) {
        TEST_ASSERT_INT_EQ(DF_OK, df_capture_retry_next(&retry, &delay));
        TEST_ASSERT_INT_EQ((int)expected_delays[i], (int)delay);
    }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_capture_retry_next(&retry, &delay));
    df_capture_retry_reset(&retry);
    TEST_ASSERT_INT_EQ(DF_OK, df_capture_retry_next(&retry, &delay));
    TEST_ASSERT_INT_EQ(250, (int)delay);
}
