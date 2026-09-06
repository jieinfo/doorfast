#include <string.h>

#include "capture.h"
#include "test.h"

void test_default_capture_filter(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("udp port 5060 or tcp port 5060",
                                 df_capture_default_filter()));
}
