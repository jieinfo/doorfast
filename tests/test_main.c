#include "test.h"

void test_config_validation(void);
void test_config_redaction(void);
void test_sip_invite_and_bye(void);
void test_policy_decisions(void);
void test_session_rejects_different_call_id(void);
void test_default_capture_filter(void);
void test_audit_format_is_redacted(void);
void test_discovery_requires_approval(void);

int test_suite_count(void) {
    return 8;
}

int main(void) {
    TEST_ASSERT_INT_EQ(8, test_suite_count());
    test_config_validation();
    test_config_redaction();
    test_sip_invite_and_bye();
    test_policy_decisions();
    test_session_rejects_different_call_id();
    test_default_capture_filter();
    test_audit_format_is_redacted();
    test_discovery_requires_approval();
    return test_failures();
}
