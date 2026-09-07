#include <string.h>

#include "capture.h"
#include "test.h"

void test_default_capture_filter(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("udp and (port 8300 or port 8302 or port 8303 or port 8304)",
                                 df_capture_default_filter()));
}
