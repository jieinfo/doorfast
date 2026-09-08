#include <string.h>

#include "capture.h"
#include "test.h"

void test_default_capture_filter(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("udp and (port 8300 or port 8302 or port 8303 or port 8304)",
                                 df_capture_default_filter()));
}

void test_capture_next_rejects_invalid_arguments(void) {
    const uint8_t *packet = NULL;
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_CAPTURE_ERROR, df_capture_next(NULL, &packet, &length));
}
