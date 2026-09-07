#include "test.h"

void test_config_validation(void);
void test_gvs_config_requires_explicit_passive_interface(void);
void test_config_redaction(void);
void test_legacy_config_import_keeps_only_safe_fields(void);
void test_sip_invite_and_bye(void);
void test_gvs_frame_validation_and_event_mapping(void);
void test_gvs_event_names(void);
void test_policy_decisions(void);
void test_session_rejects_different_call_id(void);
void test_default_capture_filter(void);
void test_audit_format_is_redacted(void);
void test_discovery_requires_approval(void);
void test_network_overlap_is_read_only(void);

int test_suite_count(void) {
    return 13;
}

int main(void) {
    TEST_ASSERT_INT_EQ(13, test_suite_count());
    test_config_validation();
    test_gvs_config_requires_explicit_passive_interface();
    test_config_redaction();
    test_legacy_config_import_keeps_only_safe_fields();
    test_sip_invite_and_bye();
    test_gvs_frame_validation_and_event_mapping();
    test_gvs_event_names();
    test_policy_decisions();
    test_session_rejects_different_call_id();
    test_default_capture_filter();
    test_audit_format_is_redacted();
    test_discovery_requires_approval();
    test_network_overlap_is_read_only();
    return test_failures();
}
