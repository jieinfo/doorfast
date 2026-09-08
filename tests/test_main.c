#include "test.h"
void test_gvs_pick_exchange(void);
void test_gvs_priority_valid_matrix_and_unknown_categories(void);
void test_gvs_session_preemption_transaction(void);
void test_gvs_observer_batch_preemption(void);
void test_gvs_replay_preemption_deadline(void);

int df_test_failure_count = 0;

void test_config_validation(void);
void test_gvs_config_requires_explicit_passive_interface(void);
void test_config_redaction(void);
void test_legacy_config_import_keeps_only_safe_fields(void);
void test_runtime_config_parses_main_gvs_section(void);
void test_runtime_config_accepts_disabled_minimal_config(void);
void test_runtime_config_rejects_ambiguous_or_unsafe_config(void);
void test_sip_invite_and_bye(void);
void test_gvs_frame_validation_and_event_mapping(void);
void test_gvs_frame_rejects_truncated_or_inconsistent_payload(void);
void test_gvs_frame_reads_payload_length_as_little_endian(void);
void test_gvs_frame_exposes_payload_from_synthetic_control_frame(void);
void test_gvs_event_names(void);
void test_gvs_identity_parses_and_filters_the_first_five_address_bytes(void);
void test_gvs_observer_only_starts_a_session_for_the_configured_identity(void);
void test_gvs_replay_reads_an_offline_control_packet_without_transmitting(void);
void test_gvs_session_tracks_passive_lifecycle(void);
void test_gvs_session_allows_a_new_call_after_end_and_rejects_old_peer(void);
void test_gvs_session_rejects_callbacks_from_an_older_generation(void);
void test_gvs_session_abort_clears_active_exchange(void);
void test_policy_decisions(void);
void test_session_rejects_different_call_id(void);
void test_default_capture_filter(void);
void test_capture_next_rejects_invalid_arguments(void);
void test_capture_retry_is_bounded_and_resets_after_recovery(void);
void test_gvs_deadline_expires_without_another_packet(void);
void test_gvs_deadline_does_not_end_a_new_generation(void);
void test_gvs_deadline_rejects_backwards_time(void);
void test_gvs_receive_drives_call_pick_sync_and_timeout(void);
void test_gvs_receive_applies_peer_hangup_and_cancels_deadline(void);
void test_audit_format_is_redacted(void);
void test_discovery_requires_approval(void);
void test_network_overlap_is_read_only(void);

int test_suite_count(void) {
    return 34;
}

int main(void) {
    test_gvs_observer_batch_preemption();
    test_gvs_replay_preemption_deadline();
    test_gvs_session_preemption_transaction();
    test_gvs_priority_valid_matrix_and_unknown_categories();
    test_gvs_pick_exchange();
    TEST_ASSERT_INT_EQ(34, test_suite_count());
    test_config_validation();
    test_gvs_config_requires_explicit_passive_interface();
    test_config_redaction();
    test_legacy_config_import_keeps_only_safe_fields();
    test_runtime_config_parses_main_gvs_section();
    test_runtime_config_accepts_disabled_minimal_config();
    test_runtime_config_rejects_ambiguous_or_unsafe_config();
    test_sip_invite_and_bye();
    test_gvs_frame_validation_and_event_mapping();
    test_gvs_frame_rejects_truncated_or_inconsistent_payload();
    test_gvs_frame_reads_payload_length_as_little_endian();
    test_gvs_frame_exposes_payload_from_synthetic_control_frame();
    test_gvs_event_names();
    test_gvs_identity_parses_and_filters_the_first_five_address_bytes();
    test_gvs_observer_only_starts_a_session_for_the_configured_identity();
    test_gvs_replay_reads_an_offline_control_packet_without_transmitting();
    test_gvs_session_tracks_passive_lifecycle();
    test_gvs_session_allows_a_new_call_after_end_and_rejects_old_peer();
    test_gvs_session_rejects_callbacks_from_an_older_generation();
    test_gvs_session_abort_clears_active_exchange();
    test_policy_decisions();
    test_session_rejects_different_call_id();
    test_default_capture_filter();
    test_capture_next_rejects_invalid_arguments();
    test_capture_retry_is_bounded_and_resets_after_recovery();
    test_gvs_deadline_expires_without_another_packet();
    test_gvs_deadline_does_not_end_a_new_generation();
    test_gvs_deadline_rejects_backwards_time();
    test_gvs_receive_drives_call_pick_sync_and_timeout();
    test_gvs_receive_applies_peer_hangup_and_cancels_deadline();
    test_audit_format_is_redacted();
    test_discovery_requires_approval();
    test_network_overlap_is_read_only();
    return test_failures();
}
