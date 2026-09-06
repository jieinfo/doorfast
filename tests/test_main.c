#include "test.h"

void test_config_validation(void);
void test_config_redaction(void);
void test_sip_invite_and_bye(void);
void test_policy_decisions(void);
void test_session_rejects_different_call_id(void);
void test_default_capture_filter(void);

int test_suite_count(void) {
    return 6;
}

int main(void) {
    TEST_ASSERT_INT_EQ(6, test_suite_count());
    test_config_validation();
    test_config_redaction();
    test_sip_invite_and_bye();
    test_policy_decisions();
    test_session_rejects_different_call_id();
    test_default_capture_filter();
    return test_failures();
}
