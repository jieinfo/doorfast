#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "runtime_id.h"
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

struct media_binding_test {
    struct df_media_module_status status;
    enum df_media_module_command command;
    uint64_t command_generation;
    uint64_t command_now_ms;
    bool command_active;
    unsigned start_calls;
    unsigned command_calls;
};

static int media_start(void *instance, uint64_t now_ms) {
    struct media_binding_test *test = instance;
    test->start_calls++;
    test->command_now_ms = now_ms;
    return DF_OK;
}

static int media_command(void *instance, enum df_media_module_command command,
    uint64_t generation, bool active, uint64_t now_ms) {
    struct media_binding_test *test = instance;
    test->command = command;
    test->command_generation = generation;
    test->command_active = active;
    test->command_now_ms = now_ms;
    test->command_calls++;
    return DF_OK;
}

static int media_status(const void *instance,
    struct df_media_module_status *status) {
    const struct media_binding_test *test = instance;
    *status = test->status;
    return DF_OK;
}

static const struct df_media_module_api_v2 media_api = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_api_v2),
    .start = media_start,
    .command = media_command,
    .status = media_status,
};

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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, NULL, 1));
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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_log_event(
        &service, 3, "event=media relay_token=secret"));
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
            .available = true,
            .encoder_running = true,
            .monitor_state = DF_GVS_MONITOR_PUBLISHING,
            .state = "publishing",
            .generation = 7,
            .status_revision = 9,
            .queue_drops = 2,
            .relay_failures = 1,
        },
    };
    struct df_runtime_media_module module = {
        .api = &media_api,
        .instance = &test,
        .available = true,
    };
    struct df_runtime_media_status status;
    const struct df_media_credentials_update credentials_update = {
        .set_rtsp_password = true,
        .rtsp_password = "must-not-write",
    };
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_start(&service));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_stop(&service, 7));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_viewer(&service, 7, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_start(&service));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_stop(&service, 7));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_ubus_monitor_viewer(&service, 7, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &call));
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&service));
    TEST_ASSERT_INT_EQ(1, test.start_calls);
    TEST_ASSERT_INT_EQ(10, (int)test.command_now_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_stop(&service, 6));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_stop(&service, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_stop(&service, 8));
    TEST_ASSERT_INT_EQ(0, test.command_calls);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_viewer(&service, 0, true));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_viewer(&service, 8, true));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_update_media_credentials(
            &service, &credentials_update));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_monitor_viewer(&service, 7, true));
    TEST_ASSERT_INT_EQ(DF_MEDIA_MODULE_COMMAND_VIEWER, test.command);
    TEST_ASSERT_INT_EQ(7, (int)test.command_generation);
    TEST_ASSERT_INT_EQ(1, test.command_active);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_monitor_stop(&service, 7));
    TEST_ASSERT_INT_EQ(DF_MEDIA_MODULE_COMMAND_STOP, test.command);
    TEST_ASSERT_INT_EQ(2, test.command_calls);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, status.available);
    TEST_ASSERT_INT_EQ(1, status.encoder_running);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_PUBLISHING, status.monitor_state);
    TEST_ASSERT_INT_EQ(7, (int)status.generation);
    TEST_ASSERT_INT_EQ(2, (int)status.queue_drops);
    TEST_ASSERT_INT_EQ(0, status.has_credential_text);
    df_runtime_ubus_stop(&service);
}

void test_runtime_ubus_monitor_start_rejects_active_call(void) {
    struct df_runtime_ubus service = {0};
    struct call_binding_test call = {0};
    struct media_binding_test media = {0};
    struct df_runtime_media_module module = {
        .api = &media_api,
        .instance = &media,
        .available = true,
    };
    unsigned sync_calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &service, provide_runtime_status, &sync_calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(
        &service, &module, "/tmp/doorfast-unused-media-credentials"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &service, provide_call_status, submit_call, &call));
    df_runtime_ubus_set_active_host(&service, true);

    call.status.session_state = DF_GVS_RINGING;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_start(&service));
    call.status.session_state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_monitor_start(&service));
    TEST_ASSERT_INT_EQ(0, (int)media.start_calls);

    call.status.session_state = DF_GVS_IDLE;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&service));
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
        .set_relay_token = true,
        .relay_token = "secret-relay",
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
    TEST_ASSERT_INT_EQ(1, status.relay_token_set);
    TEST_ASSERT_INT_EQ(0, status.has_credential_text);

    memset(&update, 0, sizeof(update));
    update.set_rtsp_password = true;
    update.rtsp_password = "";
    update.clear_relay_token = true;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_update_media_credentials(&service, &update));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_credentials_load(path, &credentials));
    TEST_ASSERT_INT_EQ(0, strcmp("secret-rtsp", credentials.rtsp_password));
    TEST_ASSERT_INT_EQ(0, credentials.relay_token[0]);

    update.clear_rtsp_password = true;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_update_media_credentials(&service, &update));
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(0, unlink(path));
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, service.runtime_id, 7));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_set_active_host(&service, true);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, NULL, 7));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, "0000000000000000", 7));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, service.runtime_id, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, service.runtime_id, 6));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_unlock(&service, service.runtime_id, 7));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, service.runtime_id, 7));
    TEST_ASSERT_INT_EQ(1, (int)calls);
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_unlock(&service, NULL, 7));
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
