#include "test.h"

void test_config_validation(void);
void test_config_redaction(void);
void test_sip_invite_and_bye(void);

int test_suite_count(void) {
    return 3;
}

int main(void) {
    TEST_ASSERT_INT_EQ(3, test_suite_count());
    test_config_validation();
    test_config_redaction();
    test_sip_invite_and_bye();
    return test_failures();
}
