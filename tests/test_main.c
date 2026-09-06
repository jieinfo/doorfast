#include "test.h"

void test_config_validation(void);
void test_config_redaction(void);

int test_suite_count(void) {
    return 2;
}

int main(void) {
    TEST_ASSERT_INT_EQ(2, test_suite_count());
    test_config_validation();
    test_config_redaction();
    return test_failures();
}
