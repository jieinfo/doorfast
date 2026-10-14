#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "runtime_id.h"
#include "runtime_ubus.h"
#include "gvs_audio_playout.h"
#include "test.h"

struct diagnostic_recording {
    char text[4096];
    size_t length;
    unsigned results;
    unsigned closed_results;
    unsigned depth;
};
static void diagnostic_string(void *context, const char *key, const char *value) {
    struct diagnostic_recording *recording = context;
    int count = snprintf(recording->text + recording->length,
        sizeof(recording->text) - recording->length, "%s=\"%s\";", key, value);
    TEST_ASSERT_INT_EQ(1, count > 0 &&
        (size_t)count < sizeof(recording->text) - recording->length);
    if (count > 0) recording->length += (size_t)count;
}
static void diagnostic_number(void *context, const char *key, uint64_t value) {
    struct diagnostic_recording *recording = context;
    int count = snprintf(recording->text + recording->length,
        sizeof(recording->text) - recording->length, "%s=%llu;", key,
        (unsigned long long)value);
    TEST_ASSERT_INT_EQ(1, count > 0 &&
        (size_t)count < sizeof(recording->text) - recording->length);
    if (count > 0) recording->length += (size_t)count;
}
static void *diagnostic_open(void *context) {
    struct diagnostic_recording *recording = context;
    recording->results++;
    TEST_ASSERT_INT_EQ(0, recording->depth);
    recording->depth++;
    return recording;
}
static void diagnostic_close(void *context, void *cookie) {
    struct diagnostic_recording *recording = context;
    TEST_ASSERT_INT_EQ(1, context == cookie);
    TEST_ASSERT_INT_EQ(1, recording->depth);
    recording->closed_results++;
    if (recording->depth > 0) recording->depth--;
}

void test_runtime_ubus_diagnostic_emitter(void) {
    struct diagnostic_recording recording = {0};
    struct df_runtime_diagnostic_sink sink = {&recording, diagnostic_string,
        diagnostic_number, diagnostic_open, diagnostic_close};
    struct df_media_session_status_v3 session = {
        .station_id = "gate_main", .generation = 8,
        .stream_name = "secret_sensitive_stream"};
    struct df_media_session_diagnostics_v2 diagnostics[] = {
        {.station_id = "gate_side", .generation = 8,
            .timer = DF_MEDIA_TIMER_FIRST_FRAME, .remaining_ms = 666},
        {.station_id = "gate_main", .generation = 7,
            .timer = DF_MEDIA_TIMER_FIRST_FRAME, .remaining_ms = 777},
        {.station_id = "gate_main", .generation = 8,
            .timer = DF_MEDIA_TIMER_FIRST_FRAME, .remaining_ms = 900},
    };
    struct df_media_session_diagnostics_v2 terminals[] = {{
        .station_id = "gate_main", .generation = 7,
        .timer = DF_MEDIA_TIMER_NONE, .end_reason = DF_MEDIA_END_UNKNOWN,
        .failure_reason = DF_MEDIA_FAILURE_FIRST_FRAME_TIMEOUT},
        {.station_id = "gate_side", .generation = 5,
         .timer = DF_MEDIA_TIMER_NONE, .end_reason = DF_MEDIA_END_LOCAL_HANGUP,
         .failure_reason = DF_MEDIA_FAILURE_CANCELLED}};
    struct df_runtime_media_status media = {.diagnostics_v2 = diagnostics,
        .diagnostics_v2_count = 3, .last_results = terminals, .last_result_count = 2};
    struct df_gvs_call_control_status call = {
        .timer = DF_GVS_CALL_TIMER_CONFIRMATION, .remaining_ms = 500,
        .end_reason = DF_GVS_CALL_END_NONE, .fault = DF_GVS_CALL_FAULT_NONE,
        .last_generation = UINT64_MAX};
    df_runtime_ubus_emit_call_diagnostics(NULL, &call, &sink);
    TEST_ASSERT_INT_EQ(0, strcmp(recording.text,
        "timer=\"confirmation\";remaining_ms=500;end_reason=\"none\";"
        "fault=\"none\";last_generation=18446744073709551615;last_station_id=\"unknown\";"));
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_diagnostics(&media, &session, &sink);
    TEST_ASSERT_INT_EQ(0, strcmp(recording.text,
        "timer=\"first_frame\";remaining_ms=900;end_reason=\"none\";failure_reason=\"none\";"));
    diagnostics[2].remaining_ms = 400;
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_diagnostics(&media, &session, &sink);
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "remaining_ms=400;") != NULL);
    media.diagnostics_v2 = NULL; /* Legacy/invalid extension, even with stale count. */
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_diagnostics(&media, &session, &sink);
    TEST_ASSERT_INT_EQ(0, strcmp(recording.text,
        "timer=\"unknown\";end_reason=\"unknown\";failure_reason=\"unknown\";"));
    media.diagnostics_v2 = diagnostics;
    session.generation = 9;
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_diagnostics(&media, &session, &sink);
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "remaining_ms=") == NULL);
    session.generation = 8;
    diagnostics[2].timer = (enum df_media_timer)999;
    diagnostics[2].end_reason = (enum df_media_end_reason)999;
    diagnostics[2].failure_reason = (enum df_media_failure_reason)999;
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_diagnostics(&media, &session, &sink);
    TEST_ASSERT_INT_EQ(0, strcmp(recording.text,
        "timer=\"unknown\";end_reason=\"unknown\";failure_reason=\"unknown\";"));
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_last_results(&media, &sink);
    TEST_ASSERT_INT_EQ(2, recording.results);
    TEST_ASSERT_INT_EQ(recording.results, recording.closed_results);
    TEST_ASSERT_INT_EQ(0, recording.depth);
    TEST_ASSERT_INT_EQ(0, strcmp(recording.text,
        "station_id=\"gate_main\";generation=7;timer=\"none\";remaining_ms=0;"
        "end_reason=\"unknown\";failure_reason=\"first_frame_timeout\";"
        "station_id=\"gate_side\";generation=5;timer=\"none\";remaining_ms=0;"
        "end_reason=\"local_hangup\";failure_reason=\"cancelled\";"));
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "password") == NULL);
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "secret_sensitive_stream") == NULL);
    media.last_results = NULL;
    recording = (struct diagnostic_recording){0};
    df_runtime_ubus_emit_media_last_results(&media, &sink);
    TEST_ASSERT_INT_EQ(0, recording.results);
    TEST_ASSERT_INT_EQ(0, recording.closed_results);
    TEST_ASSERT_INT_EQ(0, recording.depth);
    call.timer = (enum df_gvs_call_timer)999;
    call.end_reason = (enum df_gvs_call_end_reason)999;
    call.fault = (enum df_gvs_call_fault)999;
    df_runtime_ubus_emit_call_diagnostics(NULL, &call, &sink);
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "timer=\"unknown\";") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(recording.text, "remaining_ms=") == NULL);
    {
        struct df_station station = {.id = "gate_side", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 2, 0}};
        struct df_station_registry registry = {.items = &station, .count = 1};
        struct df_runtime_ubus service = {.started = true,
            .access_station_registry = &registry};
        memcpy(call.last_station, station.logical_address, 6);
        call.end_reason = DF_GVS_CALL_END_HEARTBEAT_TIMEOUT;
        call.fault = DF_GVS_CALL_FAULT_HEARTBEAT_TIMEOUT;
        call.timer = DF_GVS_CALL_TIMER_NONE;
        call.remaining_ms = 0;
        recording = (struct diagnostic_recording){0};
        df_runtime_ubus_emit_call_diagnostics(&service, &call, &sink);
        TEST_ASSERT_INT_EQ(1, strstr(recording.text,
            "last_station_id=\"gate_side\";") != NULL);
        TEST_ASSERT_INT_EQ(1, strstr(recording.text,
            "end_reason=\"heartbeat_timeout\";") != NULL);
        TEST_ASSERT_INT_EQ(1, strstr(recording.text, "remaining_ms=0;") != NULL);
        TEST_ASSERT_INT_EQ(1, strstr(recording.text, "32:02") == NULL);
    }
}

/* Catches deriving call identity from a concurrent preview or an old unlock
 * result, or emitting a station for a stale/ended/ambiguous main call. */
void test_runtime_ubus_call_station_identity_uses_current_main_call(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_station stations[] = {
        {.id = "gate_main", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 1, 0}},
        {.id = "gate_side", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 2, 0}},
    };
    struct df_station_registry registry = {.items = stations, .count = 2};
    struct df_gvs_session main_call = {.state = DF_GVS_TALKING,
        .generation = UINT64_MAX, .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_session preview = {.state = DF_GVS_PREVIEW,
        .generation = UINT64_MAX, .peer = {0x32, 2, 1, 0, 2, 0}};
    struct df_gvs_call_control_status status = {
        .session_state = DF_GVS_TALKING, .session_generation = UINT64_MAX};
    struct df_gvs_access_control access = {0};
    struct df_runtime_ubus service = {.started = true};
    const char *target;
    access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
    memcpy(access.result.destination, stations[1].logical_address, 6);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(&service,
        &access, &main_call, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(&service,
        &preview, &registry));
    target = df_runtime_ubus_call_station_id(&service, &status);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", target == NULL ? "" : target));
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(NULL, &status) == NULL);
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, NULL) == NULL);
    status.session_generation--;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    status.session_generation++;
    status.session_state = DF_GVS_RINGING;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    main_call.state = DF_GVS_RINGING;
    target = df_runtime_ubus_call_station_id(&service, &status);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", target == NULL ? "" : target));
    for (int state = DF_GVS_IDLE; state <= DF_GVS_ENDED; state++) {
        if (state == DF_GVS_RINGING || state == DF_GVS_TALKING) continue;
        status.session_state = main_call.state = (enum df_gvs_session_state)state;
        TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    }
    status.session_state = main_call.state = DF_GVS_TALKING;
    stations[0].enabled = false;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    stations[0].enabled = true;
    memcpy(stations[1].logical_address, main_call.peer, 6);
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    stations[1].enabled = false;
    target = df_runtime_ubus_call_station_id(&service, &status);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", target == NULL ? "" : target));
    main_call.peer[0] = 0x13;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    memcpy(main_call.peer, stations[0].logical_address, 6);
    main_call.generation = status.session_generation = 0;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    main_call.generation = status.session_generation = 7;
    service.access_session = NULL;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    service.access_session = &main_call;
    service.access_station_registry = NULL;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    service.access_station_registry = &registry;
    registry.items = NULL;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
    registry.items = stations;
    service.started = false;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station_id(&service, &status) == NULL);
}

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

static int ubus_audio_emit(const struct df_gvs_audio_emission *emission,
    void *context) {
    return df_gvs_audio_playout_push(context, emission);
}

static int ubus_audio_publish(const uint8_t *samples,
    const struct df_gvs_audio_chunk *chunk, void *context) {
    (void)samples;
    (void)chunk;
    (void)context;
    return 0;
}

/* Catches synthetic samples inflating real counters, jitter rejections being
 * omitted or counted again on each status read, and stale generation stats. */
void test_runtime_ubus_audio_status_merges_jitter_without_double_counting(void) {
    struct df_runtime_ubus service = {0};
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_playout playout;
    struct df_gvs_audio_jitter jitter;
    struct df_gvs_audio_status status;
    unsigned sync_calls = 0;
    uint8_t samples[160];
    memset(samples, 0x55, sizeof(samples));
    df_gvs_audio_buffer_init(&buffer);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(&playout, &buffer,
        ubus_audio_publish, NULL));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_init(&jitter, ubus_audio_emit,
        &playout));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_audio_jitter(&service, &jitter));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(&service,
        provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_audio_jitter(&service, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_audio(&service, &buffer));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_bind_audio_jitter(&service, &jitter));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_bind_audio_jitter(&service, &jitter));
    df_gvs_audio_jitter_reset(&jitter, 7);
    df_gvs_audio_playout_reset(&playout, 7);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 0));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 1));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 8, 7, 2));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 20));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 30));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 14, 7, 40));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 80));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_audio_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, status.pcm_stream_version);
    TEST_ASSERT_INT_EQ(4, (int)status.packet_count);
    TEST_ASSERT_INT_EQ(640, (int)status.byte_count);
    TEST_ASSERT_INT_EQ(800, (int)status.sample_count);
    TEST_ASSERT_INT_EQ(160, (int)status.concealed_samples);
    TEST_ASSERT_INT_EQ(1, (int)status.duplicate_packets);
    TEST_ASSERT_INT_EQ(1, (int)status.late_packets);
    TEST_ASSERT_INT_EQ(2, (int)status.reordered_packets);
    TEST_ASSERT_INT_EQ(80, (int)status.last_timestamp_ms);
    TEST_ASSERT_INT_EQ(100, (int)status.snapshot_timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_audio_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, (int)status.duplicate_packets);
    TEST_ASSERT_INT_EQ(1, (int)status.late_packets);
    df_gvs_audio_jitter_reset(&jitter, 8);
    df_gvs_audio_playout_reset(&playout, 8);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_audio_status(&service, &status));
    TEST_ASSERT_INT_EQ(8, (int)status.generation);
    TEST_ASSERT_INT_EQ(0, (int)status.duplicate_packets);
    TEST_ASSERT_INT_EQ(0, (int)status.late_packets);
    TEST_ASSERT_INT_EQ(0, (int)status.reordered_packets);
    TEST_ASSERT_INT_EQ(0, (int)status.concealed_samples);
    TEST_ASSERT_INT_EQ(0, status.snapshot_discontinuity);
    df_runtime_ubus_stop(&service);
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

struct media_binding_test {
    int status_result;
    struct df_media_module_status_v3 status;
    struct df_media_session_status_v3 sessions[2];
    enum df_media_module_command command;
    char command_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t command_generation;
    uint64_t command_now_ms;
    bool command_active;
    char start_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    unsigned start_calls;
    unsigned command_calls;
};

static int media_start(void *instance, const char *station_id,
    enum df_media_session_purpose purpose, uint64_t request_generation,
    uint64_t now_ms) {
    struct media_binding_test *test = instance;

    (void)purpose;
    (void)request_generation;
    if (strcmp(station_id, "gate_main") != 0)
        return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    test->start_calls++;
    if (test->status.required_session_count == 0U)
        test->status.required_session_count = 1U;
    (void)snprintf(test->start_station_id, sizeof(test->start_station_id),
        "%s", station_id);
    test->command_now_ms = now_ms;
    return DF_OK;
}

static int media_command(void *instance, enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    struct media_binding_test *test = instance;

    if (strcmp(key->station_id, "gate_main") != 0)
        return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (key->generation != 7U)
        return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    test->command = command;
    (void)snprintf(test->command_station_id,
        sizeof(test->command_station_id), "%s", key->station_id);
    test->command_generation = key->generation;
    test->command_active = active;
    test->command_now_ms = now_ms;
    test->command_calls++;
    return DF_OK;
}

static int media_status(const void *instance,
    struct df_media_module_status_v3 *status) {
    const struct media_binding_test *test = instance;
    if (test->status_result != DF_OK) return test->status_result;
    struct df_media_session_status_v3 *sessions = status->sessions;
    size_t capacity = status->session_count;
    size_t copied = test->status.required_session_count < capacity ?
        test->status.required_session_count : capacity;

    *status = test->status;
    status->sessions = sessions;
    status->session_count = sessions == NULL ? 0U : copied;
    if (sessions != NULL && copied > 0U)
        memcpy(sessions, test->sessions, copied * sizeof(*sessions));
    return DF_OK;
}

static const struct df_media_module_api_v3 media_api = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_api_v3),
    .start = media_start,
    .command = media_command,
    .status = media_status,
};

/* Station-call integration uses the real module/controller fixture in
 * test_runtime_station_call.c. Callback-only admission cannot own a call. */

void test_runtime_ubus_stub_validates_lifecycle_without_side_effects(void) {
    static const char station_config[] =
        "config station 'gate_main'\n"
        "\toption enabled '1'\n"
        "\toption name 'Main Gate'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_main'\n";
    const uint8_t identity[6] = {0x61, 0x02, 0x01, 1, 1, 1};
    const uint8_t station_address[6] = {0x32, 0x02, 0x01, 0, 0x02, 0};
    struct df_runtime_ubus service = {0};
    struct df_station_registry registry = {0};
    struct df_gvs_station_discovery discovery = {0};
    struct df_gvs_station_scan scan = {0};
    struct df_station_snapshot stations = {0};
    struct df_station_candidate_snapshot candidates = {0};
    struct in_addr observed_ipv4;
    unsigned calls = 0;

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(NULL, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_runtime_ubus_start(&service, NULL, &calls, 10));
    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_station_registry_parse(&registry, station_config));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_stations(&service,
        &registry, &discovery, &scan, identity, true));
    memcpy(service.runtime_id, "0123456789abcdef", sizeof(service.runtime_id));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(0, strcmp("0123456789abcdef", stations.runtime_id));
    TEST_ASSERT_INT_EQ(1, (int)stations.revision);
    TEST_ASSERT_INT_EQ(1, (int)stations.count);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", stations.stations[0].id));
    TEST_ASSERT_INT_EQ(0, strcmp("Main Gate", stations.stations[0].name));
    TEST_ASSERT_INT_EQ(0, strcmp("32:02:01:00:02:00",
        stations.stations[0].logical_address));
    TEST_ASSERT_INT_EQ(1, stations.stations[0].enabled);
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_gate_main",
        stations.stations[0].stream_name));
    TEST_ASSERT_INT_EQ(0, strcmp("none",
        stations.stations[0].route_source));
    TEST_ASSERT_INT_EQ(0, stations.stations[0].route_fresh);
    TEST_ASSERT_INT_EQ(0, stations.stations[0].monitorable);
    TEST_ASSERT_INT_EQ(0, stations.stations[0].has_last_seen);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_candidates(&service, &candidates));
    TEST_ASSERT_INT_EQ(0, strcmp("0123456789abcdef", candidates.runtime_id));
    TEST_ASSERT_INT_EQ(0, (int)candidates.count);

    TEST_ASSERT_INT_EQ(1, inet_pton(AF_INET, "10.2.1.20", &observed_ipv4));
    memcpy(discovery.candidates[0].logical_address, station_address,
        sizeof(station_address));
    discovery.candidates[0].ipv4 = observed_ipv4.s_addr;
    discovery.candidates[0].first_seen_ms = 90U;
    discovery.candidates[0].last_seen_ms = 100U;
    discovery.candidates[0].reply_count = 2U;
    discovery.candidates[0].valid = true;
    discovery.count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_station_route_observe(&service,
        station_address, observed_ipv4.s_addr, 100U, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 60099U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(0, strcmp("discovered",
        stations.stations[0].route_source));
    TEST_ASSERT_INT_EQ(1, stations.stations[0].route_fresh);
    TEST_ASSERT_INT_EQ(1, stations.stations[0].monitorable);
    TEST_ASSERT_INT_EQ(1, stations.stations[0].has_last_seen);
    TEST_ASSERT_INT_EQ(100, (int)stations.stations[0].last_seen_ms);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_candidates(&service, &candidates));
    TEST_ASSERT_INT_EQ(1, (int)candidates.count);
    TEST_ASSERT_INT_EQ(0, strcmp("32:02:01:00:02:00",
        candidates.candidates[0].logical_address));
    TEST_ASSERT_INT_EQ(0, strcmp("10.2.1.20",
        candidates.candidates[0].ipv4));
    TEST_ASSERT_INT_EQ(1, candidates.candidates[0].configured);
    TEST_ASSERT_INT_EQ(2, (int)candidates.candidates[0].reply_count);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 60100U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(0, strcmp("none",
        stations.stations[0].route_source));
    TEST_ASSERT_INT_EQ(0, stations.stations[0].route_fresh);
    TEST_ASSERT_INT_EQ(0, stations.stations[0].monitorable);

    registry.items[0].configured_ipv4 = observed_ipv4.s_addr;
    registry.items[0].route_preference = DF_STATION_ROUTE_FIXED;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 120100U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(0, strcmp("configured",
        stations.stations[0].route_source));
    TEST_ASSERT_INT_EQ(1, stations.stations[0].route_fresh);
    TEST_ASSERT_INT_EQ(1, stations.stations[0].monitorable);
    registry.items[0].enabled = false;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(0, stations.stations[0].monitorable);

    service.station_scan_enabled = false;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_station_scan(&service, 120100U));
    service.station_scan_enabled = true;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_scan(&service, 120100U));
    TEST_ASSERT_INT_EQ(1, scan.active);
    TEST_ASSERT_INT_EQ(1, df_runtime_id_is_valid(service.runtime_id) ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, service.active_host ? 1 : 0);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(1, service.active_host ? 1 : 0);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 120100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 120101U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 120099U));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_stop(&service);
    df_station_registry_destroy(&registry);
    TEST_ASSERT_INT_EQ(0, service.runtime_id[0]);
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_process(&service, 12));
}

void test_runtime_ubus_keeps_routes_for_more_than_four_configured_stations(void) {
    static const char station_config[] =
        "config station 'gate_1'\n"
        "\toption enabled '1'\n"
        "\toption name 'Gate 1'\n"
        "\toption logical_address '32:02:01:00:01:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_1'\n"
        "config station 'gate_2'\n"
        "\toption enabled '1'\n"
        "\toption name 'Gate 2'\n"
        "\toption logical_address '32:02:01:00:02:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_2'\n"
        "config station 'gate_3'\n"
        "\toption enabled '1'\n"
        "\toption name 'Gate 3'\n"
        "\toption logical_address '32:02:01:00:03:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_3'\n"
        "config station 'gate_4'\n"
        "\toption enabled '1'\n"
        "\toption name 'Gate 4'\n"
        "\toption logical_address '32:02:01:00:04:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_4'\n"
        "config station 'gate_5'\n"
        "\toption enabled '1'\n"
        "\toption name 'Gate 5'\n"
        "\toption logical_address '32:02:01:00:05:00'\n"
        "\toption ipv4 ''\n"
        "\toption route_preference 'discover_first'\n"
        "\toption stream_name 'doorfast_gate_5'\n";
    const uint8_t identity[6] = {0x61, 0x02, 0x01, 1, 1, 1};
    struct df_runtime_ubus service = {0};
    struct df_station_registry registry = {0};
    struct df_gvs_station_discovery discovery = {0};
    struct df_gvs_station_scan scan = {0};
    struct df_station_snapshot stations = {0};
    struct in_addr observed_ipv4;
    unsigned calls = 0;
    size_t index;

    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_station_registry_parse(&registry, station_config));
    TEST_ASSERT_INT_EQ(5, (int)registry.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_stations(&service,
        &registry, &discovery, &scan, identity, true));
    TEST_ASSERT_INT_EQ(1, inet_pton(AF_INET, "10.2.1.20", &observed_ipv4));
    for (index = 0U; index < registry.count; index++) {
        memcpy(discovery.candidates[index].logical_address,
            registry.items[index].logical_address, 6U);
        discovery.candidates[index].ipv4 = observed_ipv4.s_addr;
        discovery.candidates[index].first_seen_ms = 100U;
        discovery.candidates[index].last_seen_ms = 100U;
        discovery.candidates[index].reply_count = 1U;
        discovery.candidates[index].valid = true;
        discovery.count++;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_station_route_observe(&service,
            registry.items[index].logical_address, observed_ipv4.s_addr,
            100U, true));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 60099U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_station_list(&service, &stations));
    TEST_ASSERT_INT_EQ(5, (int)stations.count);
    for (index = 0U; index < stations.count; index++) {
        TEST_ASSERT_INT_EQ(0,
            strcmp("discovered", stations.stations[index].route_source));
        TEST_ASSERT_INT_EQ(1, stations.stations[index].route_fresh);
        TEST_ASSERT_INT_EQ(1, stations.stations[index].monitorable);
    }

    df_runtime_ubus_stop(&service);
    df_station_registry_destroy(&registry);
}

void test_runtime_ubus_keeps_bounded_redacted_event_log(void) {
    struct df_runtime_ubus service = {0};
    struct df_runtime_log_entry entry = {0};
    unsigned index;

    for (index = 0; index < DF_RUNTIME_UBUS_LOG_CAPACITY + 2U; index++) {
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_event(
            &service, index, "event=call generation=1"));
    }
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UBUS_LOG_CAPACITY,
                       df_runtime_ubus_log_count(&service));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_runtime_ubus_log_get(&service, 0, &entry));
    TEST_ASSERT_INT_EQ(3, entry.sequence);
    TEST_ASSERT_INT_EQ(2, entry.timestamp_ms);
    TEST_ASSERT_INT_EQ(0, strcmp("event=call generation=1", entry.message));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_log_event(
        &service, 3, "event=unlock access_material=secret"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_log_event(
        &service, 3, "event=media rtsp_password=secret"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_log_get(&service,
                           DF_RUNTIME_UBUS_LOG_CAPACITY, &entry));
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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &answer));
    memcpy(answer.runtime_id, service.runtime_id, sizeof(answer.runtime_id));
    memcpy(hangup.runtime_id, service.runtime_id, sizeof(hangup.runtime_id));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &answer));
    TEST_ASSERT_INT_EQ(11, test.submitted_at);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, test.request.type);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_submit_call(&service, &hangup));
    TEST_ASSERT_INT_EQ(2, test.submit_calls);
    answer.runtime_id[0] = answer.runtime_id[0] == '0' ? '1' : '0';
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_ubus_submit_call(&service, &answer));
    memcpy(answer.runtime_id, service.runtime_id, sizeof(answer.runtime_id));
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

void test_runtime_ubus_media_controls_require_current_generation(void) {
    struct df_runtime_ubus service = {0};
    struct call_binding_test call = {0};
    struct media_binding_test test = {
        .status = {
            .required_session_count = 1U,
            .configured_capacity = 2U,
            .effective_capacity = 2U,
            .active_encoders = 1U,
            .status_revision = 9,
            .preempted_station_id = "gate_side",
            .preempted_generation = 6U,
        },
        .sessions = {{
            .station_id = "gate_main",
            .stream_name = "doorfast_gate_main",
            .generation = 7U,
            .purpose = DF_MEDIA_SESSION_PREVIEW,
            .state = DF_MEDIA_SESSION_PUBLISHING,
            .active = true,
            .ready = true,
            .encoder_running = true,
            .queue_drops = 2U,
        }},
    };
    struct df_media_session_status_v3 module_snapshot[2];
    struct df_runtime_media_module module = {
        .api = &media_api,
        .instance = &test,
        .session_snapshot = module_snapshot,
        .session_snapshot_capacity = 2U,
        .available = true,
    };
    struct df_runtime_media_status status;
    const struct df_media_credentials_update credentials_update = {
        .set_rtsp_password = true,
        .rtsp_password = "must-not-write",
    };
    unsigned sync_calls = 0;
    uint64_t generation = 0U;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_start(&service, service.runtime_id,
            "gate_main", &generation));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_stop(&service, service.runtime_id,
            "gate_main", 7U));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_viewer(&service, service.runtime_id,
            "gate_main", 7U, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_start(&service, service.runtime_id,
            "gate_main", &generation));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_stop(&service, service.runtime_id,
            "gate_main", 7U));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_viewer(&service, service.runtime_id,
            "gate_main", 7U, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &call));
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_MEDIA_ERROR_RUNTIME_MISMATCH,
        df_runtime_ubus_monitor_start(&service, "0000000000000000",
            "gate_main", &generation));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_STATION_NOT_FOUND,
        df_runtime_ubus_monitor_start(&service, service.runtime_id,
            "missing", &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&service,
        service.runtime_id, "gate_main", &generation));
    TEST_ASSERT_INT_EQ(7, (int)generation);
    TEST_ASSERT_INT_EQ(1, test.start_calls);
    TEST_ASSERT_INT_EQ(10, (int)test.command_now_ms);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_runtime_ubus_monitor_stop(&service, service.runtime_id,
            "gate_main", 6U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_STATION_NOT_FOUND,
        df_runtime_ubus_monitor_stop(&service, service.runtime_id,
            "missing", 7U));
    TEST_ASSERT_INT_EQ(0, test.command_calls);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_runtime_ubus_monitor_viewer(&service, service.runtime_id,
            "gate_main", 8U, true));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_update_media_credentials(
            &service, &credentials_update));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_monitor_viewer(&service, service.runtime_id,
            "gate_main", 7U, true));
    TEST_ASSERT_INT_EQ(DF_MEDIA_MODULE_COMMAND_VIEWER, test.command);
    TEST_ASSERT_INT_EQ(7, (int)test.command_generation);
    TEST_ASSERT_INT_EQ(1, test.command_active);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_monitor_stop(&service, service.runtime_id,
            "gate_main", 7U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_MODULE_COMMAND_STOP, test.command);
    TEST_ASSERT_INT_EQ(2, test.command_calls);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, status.available);
    TEST_ASSERT_INT_EQ(2, (int)status.configured_capacity);
    TEST_ASSERT_INT_EQ(2, (int)status.effective_capacity);
    TEST_ASSERT_INT_EQ(1, (int)status.active_encoders);
    TEST_ASSERT_INT_EQ(1, (int)status.session_count);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_side", status.preempted_station_id));
    TEST_ASSERT_INT_EQ(6, (int)status.preempted_generation);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main",
        status.sessions[0].station_id));
    TEST_ASSERT_INT_EQ(7, (int)status.sessions[0].generation);
    TEST_ASSERT_INT_EQ(2, (int)status.sessions[0].queue_drops);
    TEST_ASSERT_INT_EQ(0, status.has_credential_text);
    test.sessions[0].state = DF_MEDIA_SESSION_FAILED;
    TEST_ASSERT_INT_EQ(0, status.diagnostics_v2_count);
    TEST_ASSERT_INT_EQ(1, status.diagnostics_v2 == NULL);
    TEST_ASSERT_INT_EQ(0, status.last_result_count);
    test.sessions[0].ready = false;
    test.sessions[0].encoder_running = false;
    test.sessions[0].last_error = DF_MEDIA_ERROR_RECONNECT_FRAME;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RECONNECT_FRAME, status.sessions[0].last_error);
    TEST_ASSERT_INT_EQ(0, status.sessions[0].ready);
    TEST_ASSERT_INT_EQ(0, status.sessions[0].encoder_running);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_monitor_start_rejects_active_call(void) {
    struct df_runtime_ubus service = {0};
    struct call_binding_test call = {0};
    struct media_binding_test media = {0};
    struct df_media_session_status_v3 module_snapshot[1];
    struct df_runtime_media_module module = {
        .api = &media_api,
        .instance = &media,
        .session_snapshot = module_snapshot,
        .session_snapshot_capacity = 1U,
        .available = true,
    };
    unsigned sync_calls = 0;
    uint64_t generation = 0U;

    media.status.required_session_count = 1U;
    media.sessions[0].active = true;
    media.sessions[0].generation = 7U;
    (void)snprintf(media.sessions[0].station_id,
        sizeof(media.sessions[0].station_id), "%s", "gate_main");

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &call));
    df_runtime_ubus_set_active_host(&service, true);

    call.status.session_state = DF_GVS_RINGING;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_start(&service, service.runtime_id,
            "gate_main", &generation));
    call.status.session_state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_start(&service, service.runtime_id,
            "gate_main", &generation));
    TEST_ASSERT_INT_EQ(0, (int)media.start_calls);

    call.status.session_state = DF_GVS_IDLE;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&service,
        service.runtime_id, "gate_main", &generation));
    TEST_ASSERT_INT_EQ(1, (int)media.start_calls);
    TEST_ASSERT_INT_EQ(3, (int)call.status_calls);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_media_credentials_preserve_blank_and_redact(void) {
    char directory[] = "/tmp/doorfast-ubus-media.XXXXXX";
    char path[256];
    struct df_runtime_ubus service = {0};
    struct df_runtime_media_module module = {0};
    struct df_runtime_media_status status;
    struct df_media_credentials credentials;
    struct df_media_credentials_update update = {
        .set_rtsp_password = true,
        .rtsp_password = "secret-rtsp",
    };
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    TEST_ASSERT_INT_EQ(1,
        snprintf(path, sizeof(path), "%s/credentials", directory) > 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_bind_media(&service, &module, path));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_update_media_credentials(&service, &update));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(0, status.available);
    TEST_ASSERT_INT_EQ(1, status.rtsp_password_set);
    TEST_ASSERT_INT_EQ(0, status.has_credential_text);

    memset(&update, 0, sizeof(update));
    update.set_rtsp_password = true;
    update.rtsp_password = "";
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_update_media_credentials(&service, &update));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_load(path, &credentials));
    TEST_ASSERT_INT_EQ(0, strcmp("secret-rtsp", credentials.rtsp_password));

    update.clear_rtsp_password = true;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_update_media_credentials(&service, &update));
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(0, unlink(path));
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}

static int submit_access(const struct df_gvs_access_request *request, void *context) {
    unsigned *calls = context;
    TEST_ASSERT_INT_EQ(1, request->destination[0] == 0x32 ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, request->session_generation > 0U ? 1 : 0);
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
    struct df_gvs_session station_session = {0};
    struct df_station station = {.id = "gate_main", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 1, 0}};
    struct df_station_registry registry = {.items = &station, .count = 1};
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct media_binding_test media = {
        .status = {.required_session_count = 1U},
        .sessions = {{.station_id = "gate_main", .generation = 7U,
            .active = true, .ready = true, .source_live = true,
            .state = DF_MEDIA_SESSION_PUBLISHING}},
    };
    struct df_media_session_status_v3 snapshot[1];
    struct df_runtime_media_module module = {.api = &media_api,
        .instance = &media, .available = true, .session_snapshot = snapshot,
        .session_snapshot_capacity = 1U};
    unsigned calls = 0;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &access, "0011223344556677", 0, submit_access, &calls));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(
        &service, &access, &session, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(
        &service, &station_session, &registry));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&service,
        &module, "/tmp/doorfast-unused-manual-credentials"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock_station(&service, "gate_main", 10));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 10));
    TEST_ASSERT_INT_EQ(1, (int)calls);
    df_runtime_ubus_stop(&service);
}

struct station_access_trace {
    unsigned calls;
    uint8_t destination[6];
};

static int submit_station_access(const struct df_gvs_access_request *request,
                                 void *context) {
    struct station_access_trace *trace = context;

    if (request == NULL || trace == NULL) return DF_ERR_INVALID;
    memcpy(trace->destination, request->destination,
        sizeof(trace->destination));
    trace->calls++;
    return DF_OK;
}

void test_runtime_ubus_unlock_targets_requested_station(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t main_address[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t side_address[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_station stations[] = {
        {.id = "gate_main", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 1, 0}},
        {.id = "gate_side", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 2, 0}},
    };
    const struct df_station_registry registry = {
        .items = stations, .count = 2,
    };
    struct df_runtime_ubus service = {0};
    struct df_gvs_access_control access;
    struct df_gvs_session session = {0};
    struct station_access_trace trace = {0};
    unsigned status_calls = 0;
    const char *target;
    struct media_binding_test media = {
        .status = {.required_session_count = 2U},
        .sessions = {
            {.station_id = "gate_main", .generation = 7U,
                .active = true, .ready = true, .source_live = true,
                .state = DF_MEDIA_SESSION_PUBLISHING},
            {.station_id = "gate_side", .generation = 8U,
                .active = true, .ready = true, .source_live = true,
                .state = DF_MEDIA_SESSION_PUBLISHING}},
    };
    struct df_media_session_status_v3 snapshot[2];
    struct df_runtime_media_module module = {.api = &media_api,
        .instance = &media, .available = true, .session_snapshot = snapshot,
        .session_snapshot_capacity = 2U};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &access, "0011223344556677", 0, submit_station_access, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &status_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(
        &service, &access, &session, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(
        &service, &session, &registry));
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&service,
        &module, "/tmp/doorfast-unused-manual-credentials"));

    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_access_station_id(NULL) == NULL);
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_access_station_id(&service) == NULL);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_side", 10));
    TEST_ASSERT_INT_EQ(1, (int)trace.calls);
    TEST_ASSERT_INT_EQ(0, memcmp(side_address, trace.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, side_address, 6));
    target = df_runtime_ubus_access_station_id(&service);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_side", target == NULL ? "" : target));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock_station(&service, "gate_main", 11));
    TEST_ASSERT_INT_EQ(1, (int)trace.calls);
    access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 11));
    /* Session cleanup must not erase attribution of a received reply. */
    memcpy(session.peer, main_address, 6);
    target = df_runtime_ubus_access_station_id(&service);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_side", target == NULL ? "" : target));
    media.sessions[0].source_live = false;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 11));
    TEST_ASSERT_INT_EQ(1, (int)trace.calls);
    /* Preparing another target is not a new protocol transaction. */
    target = df_runtime_ubus_access_station_id(&service);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_side", target == NULL ? "" : target));
    media.sessions[0].source_live = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 12));
    TEST_ASSERT_INT_EQ(2, (int)trace.calls);
    TEST_ASSERT_INT_EQ(0, memcmp(main_address, trace.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, main_address, 6));
    target = df_runtime_ubus_access_station_id(&service);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", target == NULL ? "" : target));
    /* Removed, disabled or ambiguous targets cannot be guessed. */
    stations[0].enabled = false;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_access_station_id(&service) == NULL);
    stations[0].enabled = true;
    memcpy(stations[1].logical_address, main_address, 6);
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_access_station_id(&service) == NULL);
    access.result.destination[0] = 0x13;
    TEST_ASSERT_INT_EQ(1, df_runtime_ubus_access_station_id(&service) == NULL);
    df_runtime_ubus_stop(&service);
}

/* An idle station must establish real media before emitting 04/09. */
void test_runtime_ubus_manual_unlock_defers_idle_station(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_station station = {.id = "gate_main", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 1, 0}};
    struct df_station_registry registry = {.items = &station, .count = 1};
    struct df_runtime_ubus service = {0};
    struct df_gvs_access_control access;
    struct df_gvs_session call = {0}, access_session = {0};
    struct station_access_trace trace = {0};
    struct media_binding_test media = {
        .status = {.required_session_count = 0U},
        .sessions = {{.station_id = "gate_main", .generation = 7U,
            .active = true, .state = DF_MEDIA_SESSION_REQUESTING}},
    };
    struct df_media_session_status_v3 snapshot[1];
    struct df_runtime_media_module module = {.api = &media_api,
        .instance = &media, .available = true, .session_snapshot = snapshot,
        .session_snapshot_capacity = 1U};
    unsigned calls = 0;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(&access,
        "0011223344556677", 0U, submit_station_access, &trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(&service,
        provide_runtime_status, &calls, 10U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(&service,
        &access, &call, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(&service,
        &access_session, &registry));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&service,
        &module, "/tmp/doorfast-unused-manual-credentials"));
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 10U));
    TEST_ASSERT_INT_EQ(0, (int)trace.calls);
    TEST_ASSERT_INT_EQ(1, service.manual_unlock_owns_preview);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 10U));
    TEST_ASSERT_INT_EQ(0, (int)trace.calls);
    TEST_ASSERT_INT_EQ(1, (int)media.start_calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 11U));
    TEST_ASSERT_INT_EQ(0, (int)trace.calls);
    media.sessions[0].ready = true;
    media.sessions[0].state = DF_MEDIA_SESSION_PUBLISHING;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 12U));
    TEST_ASSERT_INT_EQ(0, (int)trace.calls); /* Frozen publisher is not live. */
    media.sessions[0].source_live = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 13U));
    TEST_ASSERT_INT_EQ(1, (int)trace.calls);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock_station(&service, "gate_main", 13U));
    TEST_ASSERT_INT_EQ(1, (int)trace.calls);
    access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&service, 14U));
    TEST_ASSERT_INT_EQ(1, (int)media.command_calls);
    TEST_ASSERT_INT_EQ(DF_MEDIA_MODULE_COMMAND_STOP, media.command);
    TEST_ASSERT_INT_EQ(7, (int)media.command_generation);
    TEST_ASSERT_INT_EQ(0, service.manual_unlock_inflight);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_manual_unlock_cleanup_and_adoption(void) {
    /* terminal reply, timeout, viewer takeover, replacement, disappearance,
     * stalled startup, failed media, and HA start before viewer notification */
    for (unsigned scenario = 0U; scenario < 10U; scenario++) {
        const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
        struct df_station station = {.id = "gate_main", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 1, 0}};
        struct df_station_registry registry = {.items = &station, .count = 1};
        struct df_runtime_ubus service = {0};
        struct df_gvs_access_control access;
        struct df_gvs_session call = {0}, access_session = {0};
        struct station_access_trace trace = {0};
        struct media_binding_test media = {
            .sessions = {{.station_id = "gate_main", .generation = 7U,
                .active = true, .state = DF_MEDIA_SESSION_REQUESTING}},
        };
        struct df_media_session_status_v3 snapshot[1];
        struct df_runtime_media_module module = {.api = &media_api,
            .instance = &media, .available = true, .session_snapshot = snapshot,
            .session_snapshot_capacity = 1U};
        struct call_binding_test binding = {0};
        unsigned calls = 0U;
        uint64_t tick = 12U;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(&access,
            "0011223344556677", 0U, submit_station_access, &trace));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(&service,
            provide_runtime_status, &calls, 10U));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(&service,
            &access, &call, local));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(&service,
            &access_session, &registry));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&service,
            &module, "/tmp/doorfast-unused-manual-credentials"));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(&service,
            provide_call_status, submit_call, &binding));
        df_runtime_ubus_set_active_host(&service, true);
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_unlock_station(&service, "gate_main", 10U));
        TEST_ASSERT_INT_EQ(1, (int)media.start_calls);
        if (scenario <= 2U || scenario == 7U) {
            media.sessions[0].ready = true;
            media.sessions[0].source_live = true;
            media.sessions[0].state = DF_MEDIA_SESSION_PUBLISHING;
            TEST_ASSERT_INT_EQ(DF_OK,
                df_runtime_ubus_tick_manual_unlock(&service, 11U));
            TEST_ASSERT_INT_EQ(1, (int)trace.calls);
        }
        switch (scenario) {
        case 0U: access.result.state = DF_GVS_ACCESS_PROTOCOL_REJECTED; break;
        case 1U: access.result.state = DF_GVS_ACCESS_PROTOCOL_EXPIRED; break;
        case 2U:
            media.sessions[0].viewer_active = true;
            access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
            break;
        case 3U: media.sessions[0].generation = 8U; break;
        case 4U: media.status.required_session_count = 0U; break;
        case 5U: tick = 30010U; break;
        case 6U: media.sessions[0].state = DF_MEDIA_SESSION_FAILED; break;
        case 7U: {
            uint64_t generation = 0U;
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&service,
                service.runtime_id, "gate_main", &generation));
            access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
            break;
        }
        case 8U: media.status_result = DF_ERR_IO; tick = 30010U; break;
        case 9U:
            media.sessions[0].ready = true;
            media.sessions[0].source_live = true;
            media.sessions[0].state = DF_MEDIA_SESSION_PUBLISHING;
            service.auto_unlock_pending = true;
            call.state = DF_GVS_RINGING;
            memcpy(call.peer, station.logical_address, 6U);
            call.peer[4] = 2U;
            break;
        }
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_tick_manual_unlock(&service, tick));
        TEST_ASSERT_INT_EQ(0, service.manual_unlock_pending);
        TEST_ASSERT_INT_EQ(0, service.manual_unlock_inflight);
        TEST_ASSERT_INT_EQ((scenario == 0U || scenario == 1U ||
            scenario == 5U || scenario == 6U || scenario == 8U ||
            scenario == 9U) ? 1 : 0,
            (int)media.command_calls);
        TEST_ASSERT_INT_EQ((scenario <= 2U || scenario == 7U) ? 1 : 0,
            (int)trace.calls);
        df_runtime_ubus_stop(&service);
    }
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
        &service, service.runtime_id, DF_GVS_ELEVATOR_UP, &transaction_id));
    TEST_ASSERT_INT_EQ(99, (int)transaction_id);
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_call_elevator(
        &service, NULL, DF_GVS_ELEVATOR_DOWN, &transaction_id));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_call_elevator(
        &service, "0000000000000000", DF_GVS_ELEVATOR_DOWN,
        &transaction_id));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_elevator(
        &service, service.runtime_id, DF_GVS_ELEVATOR_DOWN, &transaction_id));
    TEST_ASSERT_INT_EQ(1, (int)transaction_id);
    TEST_ASSERT_INT_EQ(1, (int)calls);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_call_elevator(
        &service, service.runtime_id, DF_GVS_ELEVATOR_UP, &transaction_id));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_control_tick(
        &elevator, local, 2010));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 2010));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_elevator(
        &service, service.runtime_id, DF_GVS_ELEVATOR_UP, &transaction_id));
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
