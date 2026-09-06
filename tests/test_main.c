#include "test.h"

int test_suite_count(void) {
    return 0;
}

int main(void) {
    TEST_ASSERT_INT_EQ(0, test_suite_count());
    return test_failures();
}
