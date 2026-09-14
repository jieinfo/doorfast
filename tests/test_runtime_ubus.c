#include <string.h>

#include "runtime_ubus.h"
#include "test.h"

static int provide_runtime_status(
    struct df_gvs_runtime_sync_status *status, void *context) {
    unsigned *calls = context;

    if (status == NULL || calls == NULL) {
        return DF_ERR_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->phase = DF_GVS_PRESENCE_PERIODIC;
    status->role = DF_GVS_SYNC_ROLE_FOLLOWER;
    status->sync_version = 23;
    (*calls)++;
    return DF_OK;
}

struct call_binding_test {
    struct df_gvs_call_control_status status;
    struct df_runtime_call_request request;
    uint64_t submitted_at;
    unsigned status_calls;
    unsigned submit_calls;
};

static int provide_call_status(
    struct df_gvs_call_control_status *status, void *context) {
    struct call_binding_test *test = context;
    *status = test->status;
    test->status_calls++;
    return DF_OK;
}

static int submit_call(const struct df_runtime_call_request *request,
                       uint64_t now_ms, void *context) {
    struct call_binding_test *test = context;
    test->request = *request;
    test->submitted_at = now_ms;
    test->submit_calls++;
    return DF_OK;
}

void test_runtime_ubus_stub_validates_lifecycle_without_side_effects(void) {
    struct df_runtime_ubus service = {0};
    unsigned calls = 0;

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(NULL, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_runtime_ubus_start(&service, NULL, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 1));
    TEST_ASSERT_INT_EQ(0, service.active_host ? 1 : 0);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(1, service.active_host ? 1 : 0);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 11));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 9));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_stop(&service);
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 12));
}

void test_runtime_ubus_validates_and_routes_call_requests(void) {
    struct df_runtime_ubus service = {0};
    struct call_binding_test test = {0};
    struct df_gvs_call_control_status status;
    struct df_runtime_call_request answer = {
        .type = DF_GVS_CALL_COMMAND_ANSWER,
        .session_generation = 9,
        .primary_media_port = 8303,
        .secondary_media_port = 8302,
        .duration_seconds = 120,
    };
    struct df_runtime_call_request hangup = {
        .type = DF_GVS_CALL_COMMAND_HANGUP,
        .session_generation = 9,
        .reason = 1,
    };
    unsigned sync_calls = 0;

    test.status.session_state = DF_GVS_RINGING;
    test.status.session_generation = 9;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &test));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &test));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_call_status(&service, &status));
    TEST_ASSERT_INT_EQ(9, status.session_generation);
    TEST_ASSERT_INT_EQ(1, test.status_calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 11));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &answer));
    TEST_ASSERT_INT_EQ(11, test.submitted_at);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, test.request.type);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &hangup));
    TEST_ASSERT_INT_EQ(2, test.submit_calls);
    answer.primary_media_port = 0;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &answer));
    hangup.duration_seconds = 1;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &hangup));
    TEST_ASSERT_INT_EQ(2, test.submit_calls);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_reports_handshake_transport(void) {
    struct df_runtime_ubus service = {0};
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(0, strcmp("simulated",
        df_runtime_ubus_handshake_mode(&service)));
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(0, strcmp("udp",
        df_runtime_ubus_handshake_mode(&service)));
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_handshake_mode(&service) == NULL);
}

static int submit_access(const struct df_gvs_access_request *request, void *context) {
    unsigned *calls = context;
    TEST_ASSERT_INT_EQ(7, (int)request->session_generation);
    (*calls)++;
    return DF_OK;
}

void test_runtime_ubus_access_requires_active_host(void) {
    struct df_runtime_ubus service = {0};
    struct df_gvs_access_control access;
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING, .generation = 7,
        .peer = {0x32, 2, 1, 0, 1, 0},
    };
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    unsigned calls = 0;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &access, "0011223344556677", 0, submit_access, &calls));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(
        &service, &access, &session, local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 7));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 6));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock(&service, 7));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 7));
    TEST_ASSERT_INT_EQ(1, (int)calls);
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock(&service, 7));
}

static int submit_elevator(
    const struct df_gvs_elevator_request *request, void *context) {
    unsigned *calls = context;

    TEST_ASSERT_INT_EQ(0x02, request->opcode);
    (*calls)++;
    return DF_OK;
}

void test_runtime_ubus_elevator_requires_active_host_and_owns_ids(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct df_runtime_ubus service = {0};
    struct df_gvs_elevator_control elevator;
    uint64_t transaction_id = 99;
    unsigned calls = 0;
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &elevator, 10, submit_elevator, &calls));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_elevator(
        &service, &elevator, local));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_call_elevator(
        &service, DF_GVS_ELEVATOR_UP, &transaction_id));
    TEST_ASSERT_INT_EQ(99, (int)transaction_id);
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_elevator(
        &service, DF_GVS_ELEVATOR_DOWN, &transaction_id));
    TEST_ASSERT_INT_EQ(1, (int)transaction_id);
    TEST_ASSERT_INT_EQ(1, (int)calls);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_call_elevator(
        &service, DF_GVS_ELEVATOR_UP, &transaction_id));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_tick(
        &elevator, local, 2010));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 2010));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_elevator(
        &service, DF_GVS_ELEVATOR_UP, &transaction_id));
    TEST_ASSERT_INT_EQ(2, (int)transaction_id);
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_UP, elevator.request.payload[0]);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_elevator_status_is_bounded_and_aged(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    struct df_runtime_ubus service = {0};
    struct df_gvs_elevator_control elevator;
    struct df_gvs_elevator_status observed = {
        .valid = true,
        .count = 1,
        .entries = {{
            .raw_floor = -126,
            .floor = -2,
            .raw_state = 3,
            .motion = DF_GVS_ELEVATOR_STOPPED,
        }},
    };
    struct df_runtime_elevator_status status;
    unsigned calls = 0;
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_init(
        &elevator, 10, submit_elevator, &calls));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_elevator(
        &service, &elevator, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_update_elevator_status(
        &service, &observed, 20));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 30));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_elevator_status(
        &service, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_ELEVATOR_CONTROL_IDLE, status.state);
    TEST_ASSERT_INT_EQ(1, status.status_valid);
    TEST_ASSERT_INT_EQ(1, (int)status.count);
    TEST_ASSERT_INT_EQ(-2, status.entries[0].floor);
    TEST_ASSERT_INT_EQ(10, (int)status.age_ms);
    TEST_ASSERT_INT_EQ(0, status.physical_result_confirmed);
    observed.count = DF_GVS_ELEVATOR_MAX_ENTRIES + 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_update_elevator_status(&service, &observed, 31));
    df_runtime_ubus_stop(&service);
}

static int discard_audio_frame(const uint8_t *frame, size_t length,
                               void *context) {
    (void)frame;
    (void)length;
    (void)context;
    return DF_OK;
}

void test_runtime_ubus_audio_status_tracks_buffer(void) {
    struct df_runtime_ubus service = {0};
    struct df_gvs_audio_buffer audio;
    struct df_gvs_audio_tx audio_tx;
    struct df_gvs_video_frame_cache video;
    struct df_gvs_audio_status status;
    struct df_gvs_audio_tx_status tx_status;
    struct df_gvs_video_status video_status;
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
    const uint8_t payload[] = {1, 2, 3};
    unsigned sync_calls = 0;
    df_gvs_audio_buffer_init(&audio);
    df_gvs_video_frame_cache_init(&video);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &audio_tx, 77, discard_audio_frame, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_audio(&service, &audio));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_audio(&service, &audio));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_bind_audio_tx(&service, &audio_tx));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_audio_tx(&service, &audio_tx));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_video(&service, &video));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_video(&service, &video));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &audio, payload, sizeof(payload), 1, 4, 20));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(
        &audio, 4, sizeof(payload), 21));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_audio_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, status.ready);
    TEST_ASSERT_INT_EQ(3, (int)status.buffered_bytes);
    TEST_ASSERT_INT_EQ(4, (int)status.generation);
    TEST_ASSERT_INT_EQ(0, (int)status.missing_packets);
    TEST_ASSERT_INT_EQ(0, (int)status.duplicate_packets);
    TEST_ASSERT_INT_EQ(0, (int)status.late_packets);
    TEST_ASSERT_INT_EQ(1, status.snapshot_ready);
    TEST_ASSERT_INT_EQ(1, (int)status.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(0, (int)status.snapshot_previous_packet_count);
    TEST_ASSERT_INT_EQ(3, (int)status.snapshot_source_bytes);
    TEST_ASSERT_INT_EQ(0, (int)status.snapshot_dropped_bytes);
    TEST_ASSERT_INT_EQ(50, (int)status.snapshot_bytes);
    TEST_ASSERT_INT_EQ(21, (int)status.snapshot_timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_audio_tx_status(&service, &tx_status));
    TEST_ASSERT_INT_EQ(0, tx_status.active);
    TEST_ASSERT_INT_EQ(77, tx_status.next_sequence);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_video_status(&service, &video_status));
    TEST_ASSERT_INT_EQ(0, video_status.ready);
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_store(
        &video, jpeg, sizeof(jpeg), 4, 12, 30));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_video_status(&service, &video_status));
    TEST_ASSERT_INT_EQ(1, video_status.ready);
    TEST_ASSERT_INT_EQ(4, (int)video_status.generation);
    TEST_ASSERT_INT_EQ(12, video_status.frame_no);
    TEST_ASSERT_INT_EQ(4, (int)video_status.bytes);
    TEST_ASSERT_INT_EQ(30, (int)video_status.timestamp_ms);
    df_runtime_ubus_stop(&service);
    df_gvs_video_frame_cache_reset(&video);
}
