#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "doorfast.h"
#include "media_session_manager.h"
#include "runtime_ubus.h"
#include "test.h"

void test_media_video_reject_reason_names(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("destination_mismatch",
        df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_DESTINATION)));
    TEST_ASSERT_INT_EQ(0, strcmp("source_ipv4_mismatch",
        df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_SOURCE_IPV4)));
    TEST_ASSERT_INT_EQ(0, strcmp("monitor_mismatch",
        df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_MONITOR)));
    TEST_ASSERT_INT_EQ(0, strcmp("jpeg_invalid",
        df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_JPEG)));
    TEST_ASSERT_INT_EQ(0, strcmp("encoder_failed",
        df_media_video_reject_reason_name(DF_MEDIA_VIDEO_REJECT_ENCODER)));
}

/* The production handoff must keep the real monitor's media watchdog alive,
 * restore an unsent bound preview, and never carry handoff flags into incoming. */
void test_media_session_manager_outbound_watchdogs_and_incoming_replacement(void);
void test_media_session_manager_outbound_bound_rollback_preserves_pipeline(void);
void test_media_session_manager_outbound_negative_gates(void);


/* Loss of the real first-frame/recovery boundary or reuse of a prior public
 * generation must fail these observations, while control timing stays real. */
void test_media_session_manager_observability(void);

struct manager_trace {
    uint64_t available_kib;
    const char *unroutable_station;
    const char *failing_resource_station;
    unsigned resource_starts;
    unsigned resource_stops;
    uint64_t proposed_generation;
    uint64_t session_generation_at_start;
    unsigned control_count;
    uint8_t last_control_family;
    uint8_t last_control_opcode;
    size_t last_control_payload_length;
    uint8_t last_control_destination[6];
    uint8_t last_control_source[6];
    bool fail_control;
};

static int manager_emit_control(const uint8_t destination[6],
    uint32_t destination_ipv4, const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, size_t payload_length,
    void *context) {
    struct manager_trace *trace = context;
    (void)destination_ipv4;
    (void)payload;
    if (trace != NULL) {
        trace->control_count++;
        trace->last_control_family = family;
        trace->last_control_opcode = opcode;
        trace->last_control_payload_length = payload_length;
        memcpy(trace->last_control_destination, destination,
            sizeof(trace->last_control_destination));
        memcpy(trace->last_control_source, source,
            sizeof(trace->last_control_source));
    }
    return trace != NULL && trace->fail_control ? DF_ERR_IO : DF_OK;
}

static int manager_resolve_route(const uint8_t peer[6], uint64_t now_ms,
    uint32_t *ipv4, void *context) {
    struct manager_trace *trace = context;
    (void)now_ms;

    if (peer == NULL || ipv4 == NULL || trace == NULL) return DF_ERR_INVALID;
    if (trace->unroutable_station != NULL &&
        peer[5] == (uint8_t)trace->unroutable_station[0]) return DF_ERR_IO;
    *ipv4 = 0x01020300U | peer[5];
    return DF_OK;
}

static int manager_available_memory(uint64_t *available_kib, void *context) {
    struct manager_trace *trace = context;

    if (available_kib == NULL || trace == NULL) return DF_ERR_INVALID;
    *available_kib = trace->available_kib;
    return DF_OK;
}

static int manager_start_resources(struct df_media_session *session,
    uint64_t proposed_generation, void *context) {
    struct manager_trace *trace = context;

    if (session == NULL || proposed_generation == 0U || trace == NULL)
        return DF_ERR_INVALID;
    trace->resource_starts++;
    trace->proposed_generation = proposed_generation;
    trace->session_generation_at_start = session->generation;
    if (trace->failing_resource_station != NULL &&
        strcmp(session->station_id, trace->failing_resource_station) == 0)
        return DF_ERR_IO;
    return DF_OK;
}

static void manager_stop_resources(struct df_media_session *session,
    void *context) {
    struct manager_trace *trace = context;

    if (session != NULL && trace != NULL) trace->resource_stops++;
}

static void manager_fixture(struct df_media_module_config_v3 *config,
    struct df_media_module_callbacks_v3 *callbacks,
    struct manager_trace *trace) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0U, 0U, 0U, 0U, 0U, 'm'}},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0U, 0U, 0U, 0U, 0U, 's'}},
        {.id = "gate_garage", .stream_name = "doorfast_gate_garage",
         .enabled = true, .logical_address = {0U, 0U, 0U, 0U, 0U, 'g'}},
        {.id = "gate_disabled", .stream_name = "doorfast_gate_disabled",
         .enabled = false, .logical_address = {0U, 0U, 0U, 0U, 0U, 'd'}},
    };

    memset(config, 0, sizeof(*config));
    config->enabled = true;
    config->stations = stations;
    config->station_count = sizeof(stations) / sizeof(stations[0]);
    config->max_encoders = 2U;
    config->incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW;
    config->min_free_kib = 1024U;
    config->fps = 10U;

    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->emit_control = manager_emit_control;
    callbacks->resolve_route = manager_resolve_route;
    callbacks->available_memory = manager_available_memory;
    callbacks->context = trace;
}

static void manager_assert_current_diagnostic(
    const struct df_media_session_manager *manager, uint64_t now_ms,
    enum df_media_failure_reason failure, enum df_media_timer timer,
    uint64_t remaining_ms) {
    struct df_media_session_diagnostics_v2 entry;
    struct df_media_module_diagnostics_v2 status = {
        .sessions = &entry, .session_count = 1U,
    };
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        manager, now_ms, &status));
    TEST_ASSERT_INT_EQ(1, status.session_count);
    TEST_ASSERT_INT_EQ(0, status.required_last_result_count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, entry.end_reason);
    TEST_ASSERT_INT_EQ(failure, entry.failure_reason);
    TEST_ASSERT_INT_EQ(timer, entry.timer);
    TEST_ASSERT_INT_EQ(remaining_ms, entry.remaining_ms);
    TEST_ASSERT_INT_EQ(manager->sessions[0].generation, entry.generation);
}

/* Run the real monitor overflow branches, preserving any earlier evidenced
 * first-frame cause instead of manufacturing an error-free terminal result. */
void test_media_session_manager_monitor_overflow_diagnostic(void) {
    static const struct df_media_station_config_v3 station = {
        .id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
        .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}, .ipv4 = 0x01020304U,
    };
    static const uint8_t peer_stop[] = {1U};
    static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    unsigned scenario;

    for (scenario = 0U; scenario < 2U; scenario++) {
        struct df_media_module_config_v3 config;
        struct df_media_module_callbacks_v3 callbacks;
        struct manager_trace trace = {.available_kib = 4096U};
        struct df_media_session_manager manager = {0};
        struct df_media_session_diagnostics_v2 last;
        struct df_media_module_diagnostics_v2 diagnostics = {
            .last_results = &last, .last_result_count = 1U,
        };
        struct df_gvs_frame frame = {.family = 0x03U};
        uint64_t generation = 0U;

        manager_fixture(&config, &callbacks, &trace);
        config.stations = &station;
        config.station_count = 1U;
        config.max_encoders = 1U;
        config.local[0] = 0x61U;
        config.first_frame_timeout_s = 1U;
        TEST_ASSERT_INT_EQ(DF_OK,
            df_media_session_manager_init(&manager, &config, &callbacks));
        manager.next_generation = UINT64_MAX - 1U;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
            "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
        TEST_ASSERT_INT_EQ(1, generation == UINT64_MAX);
        memcpy(frame.source, station.logical_address, sizeof(frame.source));
        memcpy(frame.destination, config.local, sizeof(frame.destination));
        if (scenario == 0U) {
            frame.opcode = 0x02U;
            frame.payload = peer_stop;
            frame.payload_length = sizeof(peer_stop);
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
                &manager, &frame, station.ipv4, 101U));
        } else {
            frame.opcode = 0x84U;
            frame.payload = confirmation;
            frame.payload_length = sizeof(confirmation);
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
                &manager, &frame, station.ipv4, 101U));
            TEST_ASSERT_INT_EQ(DF_OK,
                df_media_session_manager_tick(&manager, 1101U));
            TEST_ASSERT_INT_EQ(DF_OK,
                df_media_session_manager_tick(&manager, 1102U));
            TEST_ASSERT_INT_EQ(DF_OK,
                df_media_session_manager_tick(&manager, 2102U));
        }
        TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
            &manager, 2200U, &diagnostics));
        TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
        TEST_ASSERT_INT_EQ(1, last.generation == UINT64_MAX);
        TEST_ASSERT_INT_EQ(0, strcmp("gate_main", last.station_id));
        TEST_ASSERT_INT_EQ(DF_MEDIA_END_UNKNOWN, last.end_reason);
        TEST_ASSERT_INT_EQ(scenario == 0U ? DF_MEDIA_FAILURE_UNKNOWN :
            DF_MEDIA_FAILURE_FIRST_FRAME_TIMEOUT, last.failure_reason);
        TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_NONE, last.timer);
        TEST_ASSERT_INT_EQ(0, last.remaining_ms);
        df_media_session_manager_destroy(&manager);
    }
}

void test_media_session_manager_diagnostics_capacity(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_diagnostics_v2 entries[3];
    struct df_media_module_diagnostics_v2 diagnostics;
    struct df_media_module_status_v3 status = {0};
    size_t index;
    uint64_t generation, rejected = 999U, revision;
    manager_fixture(&config, &callbacks, &trace);
    config.max_encoders = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    for (index = 0U; index < 3U; index++) {
        struct df_media_session_key key = {.station_id = config.stations[index].id};
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
            key.station_id, DF_MEDIA_SESSION_PREVIEW, 100U + index, &generation));
        key.generation = generation;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        revision = status.status_revision;
        if (index == 0U) {
            TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
                df_media_session_manager_start(&manager, "gate_side",
                    DF_MEDIA_SESSION_PREVIEW, 100U, &rejected));
            TEST_ASSERT_INT_EQ(0, rejected);
        }
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 100U + index));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        TEST_ASSERT_INT_EQ(1, status.status_revision > revision);
        TEST_ASSERT_INT_EQ(0, status.required_session_count);
    }
    memset(entries, 0xa5, sizeof(entries));
    diagnostics = (struct df_media_module_diagnostics_v2){
        .last_results = entries, .last_result_count = 1U,
    };
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 110U, &diagnostics));
    TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
    TEST_ASSERT_INT_EQ(3, diagnostics.required_last_result_count);
    TEST_ASSERT_INT_EQ(0xa5, ((unsigned char *)&entries[1])[0]);
    diagnostics.last_result_count = 3U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 110U, &diagnostics));
    TEST_ASSERT_INT_EQ(3, diagnostics.last_result_count);
    for (index = 0U; index < 3U; index++) {
        TEST_ASSERT_INT_EQ(0, strcmp(config.stations[index].id,
            entries[index].station_id));
        TEST_ASSERT_INT_EQ(DF_MEDIA_END_LOCAL_HANGUP, entries[index].end_reason);
        TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_CANCELLED, entries[index].failure_reason);
    }
    df_media_session_manager_destroy(&manager);
}

static void manager_assert_bridge_snapshot(struct df_media_session_manager *manager) {
    char directory[] = "/tmp/doorfast-media-diagnostics-XXXXXX";
    char path[256];
    struct df_runtime_media_module module = {
        .api = &df_media_module_api_v3, .instance = manager, .available = true,
        .diagnostics_api = &df_media_module_diagnostics_api_v1,
        .diagnostics_api_v2 = &df_media_module_diagnostics_api_v2,
        .session_snapshot_capacity = 4U,
    };
    struct df_runtime_ubus service = {.started = true, .last_now_ms = 201U};
    struct df_runtime_media_status status;
    struct df_gvs_monitor before = manager->sessions[0].monitor;
    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    (void)snprintf(path, sizeof(path), "%s/credentials", directory);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&service, &module, path));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(1, status.diagnostics_v2_count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_FIRST_FRAME, status.diagnostics_v2[0].timer);
    TEST_ASSERT_INT_EQ(900, status.diagnostics_v2[0].remaining_ms);
    TEST_ASSERT_INT_EQ(1, status.diagnostics_count);
    TEST_ASSERT_INT_EQ(101, status.diagnostics[0].confirmed_ms);
    TEST_ASSERT_INT_EQ(0, status.last_result_count);
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &manager->sessions[0].monitor, sizeof(before)));
    module.diagnostics_api_v2 = NULL;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_INT_EQ(0, status.diagnostics_v2_count);
    TEST_ASSERT_INT_EQ(1, status.diagnostics_v2 == NULL);
    TEST_ASSERT_INT_EQ(0, status.last_result_count);
    TEST_ASSERT_INT_EQ(1, status.last_results == NULL);
    TEST_ASSERT_INT_EQ(1, status.diagnostics_count); /* v1 counters still work */
    df_runtime_ubus_stop(&service);
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}

void test_media_session_manager_observability(void) {
    static const struct df_media_station_config_v3 station = {
        .id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
        .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}, .ipv4 = 0x01020304U,
    };
    static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_diagnostics_v2 current[2], last[4];
    struct df_media_module_diagnostics_v2 diagnostics = {
        .sessions = current, .session_count = 2U,
        .last_results = last, .last_result_count = 4U,
    };
    struct df_media_module_status_v3 status = {0};
    struct df_gvs_frame frame = {
        .family = 3U, .opcode = 0x84U,
        .payload = confirmation, .payload_length = sizeof(confirmation),
    };
    struct df_media_session_key key = {.station_id = "gate_main"};
    uint64_t revision, generation;
    struct df_gvs_monitor before;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = &station;
    config.station_count = 1U;
    config.max_encoders = 1U;
    config.local[0] = 0x61U;
    config.first_frame_timeout_s = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    key.generation = generation;
    memcpy(frame.source, config.stations[0].logical_address, 6U);
    memcpy(frame.destination, config.local, 6U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
        &manager, &frame, 0x01020304U, 101U));
    manager_assert_bridge_snapshot(&manager);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    revision = status.status_revision;
    before = manager.sessions[0].monitor;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 201U, &diagnostics));
    TEST_ASSERT_INT_EQ(1, diagnostics.session_count);
    TEST_ASSERT_INT_EQ(0, diagnostics.last_result_count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_FIRST_FRAME, current[0].timer);
    TEST_ASSERT_INT_EQ(900, current[0].remaining_ms);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, current[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_NONE, current[0].failure_reason);
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 301U, &diagnostics));
    TEST_ASSERT_INT_EQ(800, current[0].remaining_ms);
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &manager.sessions[0].monitor,
        sizeof(before)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(revision, status.status_revision);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 1101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 1102U));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 1202U, &diagnostics));
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_FIRST_FRAME_TIMEOUT,
        current[0].failure_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, current[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_RECONNECT, current[0].timer);
    TEST_ASSERT_INT_EQ(900, current[0].remaining_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 1203U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 2102U));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 2102U, &diagnostics));
    TEST_ASSERT_INT_EQ(0, diagnostics.session_count);
    TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
    TEST_ASSERT_INT_EQ(generation, last[0].generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_LOCAL_HANGUP, last[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_CANCELLED, last[0].failure_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_NONE, last[0].timer);
    TEST_ASSERT_INT_EQ(0, last[0].remaining_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 6100U, &generation));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 6100U, &diagnostics));
    TEST_ASSERT_INT_EQ(generation, current[0].generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, current[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_NONE, current[0].failure_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_NONE, current[0].timer);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(&manager,
        "gate_main", 9U, 6101U));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 6101U, &diagnostics));
    TEST_ASSERT_INT_EQ(generation, last[0].generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_PREEMPTED, last[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_PREEMPTED, last[0].failure_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, current[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_NONE, current[0].failure_reason);
    key.generation = current[0].generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 6102U));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 6202U, &diagnostics));
    TEST_ASSERT_INT_EQ(1, diagnostics.session_count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_NONE, current[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_CANCELLED, current[0].failure_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_NONE, current[0].timer);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_PREEMPTED, last[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 7102U));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 7102U, &diagnostics));
    TEST_ASSERT_INT_EQ(0, diagnostics.session_count);
    TEST_ASSERT_INT_EQ(key.generation, last[0].generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_LOCAL_HANGUP, last[0].end_reason);
    TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_CANCELLED, last[0].failure_reason);
    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    diagnostics.session_count = 2U; diagnostics.last_result_count = 4U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
        &manager, 6200U, &diagnostics));
    TEST_ASSERT_INT_EQ(0, diagnostics.last_result_count);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_outbound_call_lock(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    uint64_t preview_generation = 0U;
    uint64_t call_generation = 0U;
    uint64_t second_generation = 99U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U,
        &preview_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_CALL, 101U,
        &call_generation));
    TEST_ASSERT_INT_EQ(1, call_generation != preview_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL,
        df_media_session_manager_find(&manager, "gate_main")->purpose);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_CALL, 102U, &second_generation));
    TEST_ASSERT_INT_EQ(0, second_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_STATION_NOT_FOUND,
        df_media_session_manager_start(&manager, "missing",
            DF_MEDIA_SESSION_CALL, 103U, &second_generation));
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_outbound_call_requires_current_preview_confirmation(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    const struct df_media_session *session;
    struct df_media_station_config_v3 stations[4];
    struct df_gvs_frame confirmation = {0};
    static const uint8_t confirmation_payload[] = {0x1eU, 0x00U, 0x01U};
    uint64_t preview_generation = 0U;
    uint64_t call_generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.local[0] = 0x61U;
    config.min_free_kib = 0U;
    memcpy(stations, config.stations, sizeof(stations));
    stations[0].ipv4 = 0x01020304U;
    memcpy(stations[0].logical_address,
        (const uint8_t[]){0x32U, 2U, 1U, 0U, 2U, 0U}, 6U);
    config.stations = stations;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &preview_generation));
    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER,
        &(struct df_media_session_key){.station_id = "gate_main",
            .generation = preview_generation}, true, 101U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED,
        df_media_module_api_v3.start(&manager, "gate_main",
            DF_MEDIA_SESSION_CALL, 0U, 102U));
    TEST_ASSERT_INT_EQ(0, call_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, session->purpose);
    TEST_ASSERT_INT_EQ(preview_generation, session->generation);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);

    confirmation.family = 0x03U;
    confirmation.opcode = 0x84U;
    memcpy(confirmation.source, session->station, sizeof(confirmation.source));
    memcpy(confirmation.destination, config.local,
        sizeof(confirmation.destination));
    confirmation.payload = confirmation_payload;
    confirmation.payload_length = sizeof(confirmation_payload);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
        &manager, &confirmation, session->station_ipv4, 103U));
    TEST_ASSERT_INT_EQ(1, session->preview_confirmation_current ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, session->monitor.state);
    TEST_ASSERT_INT_EQ(0, session->monitor.retry_after_stop ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, session->monitor.retry_waiting ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, session->monitor.first_frame_deadline_ms != 0U ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
        "gate_main", DF_MEDIA_SESSION_CALL, 0U, 104U));
    call_generation = session->generation;
    TEST_ASSERT_INT_EQ(preview_generation, call_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL, session->purpose);
    TEST_ASSERT_INT_EQ(preview_generation, session->generation);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);
    df_media_session_manager_destroy(&manager);
}

void test_media_module_api_starts_outbound_call_without_incoming_generation(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_module_status_v3 status = {0};
    struct df_media_session_status_v3 entries[4] = {0};

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    status.sessions = entries;
    status.session_count = sizeof(entries) / sizeof(entries[0]);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED,
        df_media_module_api_v3.start(
        &manager, "gate_main", DF_MEDIA_SESSION_CALL, 0U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, status.session_count);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_admits_dynamic_station_pool(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key first_key;
    const struct df_media_session *found;
    uint64_t first_generation = 0U;
    uint64_t reused_generation = 0U;
    uint64_t second_generation = 0U;
    uint64_t rejected_generation = 99U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_capacity(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U,
        &first_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 101U,
        &reused_generation));
    TEST_ASSERT_INT_EQ(first_generation, reused_generation);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_side", DF_MEDIA_SESSION_PREVIEW, 102U,
        &second_generation));
    TEST_ASSERT_INT_EQ(1, second_generation != first_generation);
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_start(&manager, "gate_garage",
            DF_MEDIA_SESSION_PREVIEW, 103U, &rejected_generation));
    TEST_ASSERT_INT_EQ(0, rejected_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_STATION_DISABLED,
        df_media_session_manager_start(&manager, "gate_disabled",
            DF_MEDIA_SESSION_PREVIEW, 104U, &rejected_generation));
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_active(&manager));

    first_key.station_id = "gate_main";
    first_key.generation = first_generation;
    found = df_media_session_manager_lookup(&manager, &first_key);
    TEST_ASSERT_INT_EQ(1, found != NULL);
    TEST_ASSERT_INT_EQ(first_generation, found == NULL ? 0U : found->generation);
    first_key.generation++;
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &first_key) == NULL);
    df_media_session_manager_destroy(&manager);

    config.max_encoders = 0U;
    first_generation = 0U;
    rejected_generation = 99U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_capacity(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 105U,
        &first_generation));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 106U, &rejected_generation));
    TEST_ASSERT_INT_EQ(0, rejected_generation);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_rejects_admission_without_mutating_sessions(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks resource_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_media_session_key first_key;
    uint64_t first_generation = 0U;
    uint64_t generation = 88U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &resource_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 200U,
        &first_generation));
    first_key.station_id = "gate_main";
    first_key.generation = first_generation;

    trace.available_kib = 100U;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RESOURCE_EXHAUSTED,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 201U, &generation));
    TEST_ASSERT_INT_EQ(0, generation);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &first_key) != NULL);

    trace.available_kib = 4096U;
    trace.unroutable_station = "s";
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ROUTE_UNAVAILABLE,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 202U, &generation));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));

    trace.unroutable_station = NULL;
    trace.failing_resource_station = "gate_side";
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 203U, &generation));
    TEST_ASSERT_INT_EQ(0, generation);
    TEST_ASSERT_INT_EQ(first_generation + 1U, trace.proposed_generation);
    TEST_ASSERT_INT_EQ(0, trace.session_generation_at_start);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1, trace.resource_stops);
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &first_key) != NULL);

    trace.failing_resource_station = NULL;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_side", DF_MEDIA_SESSION_PREVIEW, 204U,
        &generation));
    TEST_ASSERT_INT_EQ(first_generation + 1U, generation);
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_active(&manager));
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_commands_require_exact_key(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    uint64_t generation = 0U;
    uint64_t replacement_generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 300U, &generation));
    key.station_id = "gate_main";
    key.generation = generation + 1U;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 301U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    key.generation = generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 302U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_CALL, 303U,
        &replacement_generation));
    TEST_ASSERT_INT_EQ(1, replacement_generation != generation);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_status_revisions_are_monotonic(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {
        .sessions = &entry,
        .session_count = 1U,
    };
    struct df_media_session_key key = {.station_id = "gate_main"};
    uint64_t empty_revision;
    uint64_t started_revision;
    uint64_t viewer_revision;
    uint64_t hidden_revision;
    uint64_t session_revision;
    uint64_t replacement_generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    empty_revision = status.status_revision;
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ((int)empty_revision, (int)status.status_revision);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 300U, &key.generation));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    started_revision = status.status_revision;
    session_revision = entry.status_revision;
    TEST_ASSERT_INT_EQ(1, started_revision > empty_revision);
    TEST_ASSERT_INT_EQ(1, session_revision > 0U);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 301U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    viewer_revision = status.status_revision;
    TEST_ASSERT_INT_EQ((int)(started_revision + 1U), (int)viewer_revision);
    TEST_ASSERT_INT_EQ((int)(session_revision + 1U),
        (int)entry.status_revision);
    session_revision = entry.status_revision;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, false, 302U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    hidden_revision = status.status_revision;
    TEST_ASSERT_INT_EQ(1, hidden_revision > viewer_revision);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > session_revision);
    session_revision = entry.status_revision;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 303U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, status.status_revision > hidden_revision);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > session_revision);
    viewer_revision = status.status_revision;
    session_revision = entry.status_revision;

    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ((int)viewer_revision, (int)status.status_revision);
    TEST_ASSERT_INT_EQ((int)session_revision, (int)entry.status_revision);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 304U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 305U,
        &replacement_generation));
    TEST_ASSERT_INT_EQ(1, replacement_generation > key.generation);
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, status.status_revision > viewer_revision);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > session_revision);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_revisions_advance_without_status_reads(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    uint64_t generation_a = 0U;
    uint64_t generation_b = 0U;
    uint64_t manager_revision;
    uint64_t session_revision;

    manager_fixture(&config, &callbacks, &trace);
    config.max_encoders = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation_a));
    manager_revision = manager.status_revision;
    session_revision = manager.sessions[0].status_revision;

    key.station_id = "gate_main";
    key.generation = generation_a;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_side", DF_MEDIA_SESSION_PREVIEW, 102U, &generation_b));
    key.station_id = "gate_side";
    key.generation = generation_b;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 104U, &generation_a));

    TEST_ASSERT_INT_EQ(1, manager.status_revision > manager_revision);
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].status_revision > session_revision);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_rejects_preview_key_after_call_upgrade(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key preview_key;
    uint64_t preview_generation = 0U;
    uint64_t call_generation;
    const struct df_media_session *session;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 200U, &preview_generation));
    preview_key.station_id = "gate_main";
    preview_key.generation = preview_generation;
    call_generation = preview_generation;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(&manager,
        "gate_main", call_generation, 201U));
    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(1, session != NULL &&
        session->generation != preview_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_VIEWER, &preview_key, true, 202U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_STOP, &preview_key, false, 203U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_dispatches_independent_monitor_controls(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U}},
    };
    const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t main_station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t side_station[6] = {0x32U, 2U, 1U, 0U, 3U, 0U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {0};
    uint64_t main_generation = 0U;
    uint64_t side_generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &main_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_side", DF_MEDIA_SESSION_PREVIEW, 101U, &side_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 101U));
    TEST_ASSERT_INT_EQ(2, (int)trace.control_count);

    memcpy(frame.source, main_station, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x84U;
    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020305U, 102U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 102U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO,
        manager.sessions[0].monitor.state);

    memcpy(frame.source, side_station, sizeof(frame.source));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 103U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO,
        manager.sessions[1].monitor.state);
    TEST_ASSERT_INT_EQ(1, main_generation != side_generation);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_replies_to_preview_keepalive(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
    };
    const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {0};
    uint64_t generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 100U));

    /* A station may send its keepalive before the preview confirmation has
     * arrived.  Every keepalive belongs to the same preview generation and
     * must be acknowledged without advancing or tearing down the monitor. */
    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x51U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 100U + 1U));
    TEST_ASSERT_INT_EQ(2, (int)trace.control_count);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING,
        manager.sessions[0].monitor.state);
    TEST_ASSERT_INT_EQ((int)generation,
        (int)manager.sessions[0].monitor.generation);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 100U + 2U));
    TEST_ASSERT_INT_EQ(3, (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x52, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING,
        manager.sessions[0].monitor.state);

    frame.opcode = 0x84U;
    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 103U));

    frame.opcode = 0x51U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020301U, 104U));
    TEST_ASSERT_INT_EQ(3, (int)trace.control_count);
    frame.destination[5] ^= 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 104U));
    TEST_ASSERT_INT_EQ(3, (int)trace.control_count);
    frame.destination[5] ^= 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 105U));
    TEST_ASSERT_INT_EQ(4, (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x03, trace.last_control_family);
    TEST_ASSERT_INT_EQ(0x52, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(0, (int)trace.last_control_payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(trace.last_control_destination,
        stations[0].logical_address,
        sizeof(trace.last_control_destination)));
    TEST_ASSERT_INT_EQ(0, memcmp(trace.last_control_source, local,
        sizeof(trace.last_control_source)));

    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 106U));
    TEST_ASSERT_INT_EQ(4, (int)trace.control_count);

    frame.payload = NULL;
    frame.payload_length = 0U;
    trace.fail_control = true;
    {
        const struct df_gvs_monitor before = manager.sessions[0].monitor;

        TEST_ASSERT_INT_EQ(DF_ERR_IO,
            df_media_session_manager_receive_control(&manager, &frame,
                0x01020300U, 107U));
        TEST_ASSERT_INT_EQ(0, memcmp(&before, &manager.sessions[0].monitor,
            sizeof(before)));
    }
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_cleans_busy_preview_after_three_rejections(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {0};
    const struct df_media_session *session;
    uint64_t generation = 0U;
    uint64_t now_ms = 100U;
    unsigned attempt;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, now_ms, &generation));

    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x50U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    for (attempt = 0U; attempt < DF_GVS_MONITOR_STARTUP_BUSY_RETRY_LIMIT;
         attempt++) {
        TEST_ASSERT_INT_EQ(DF_OK,
            df_media_session_manager_tick(&manager, now_ms));
        TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
        TEST_ASSERT_INT_EQ(DF_OK,
            df_media_session_manager_receive_control(&manager, &frame,
                stations[0].ipv4, now_ms + 1U));
        {
            struct df_media_session_status_v3 entry;
            struct df_media_module_status_v3 status = {
                .sessions = &entry, .session_count = 1U,
            };
            TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
            TEST_ASSERT_INT_EQ(0, entry.ready);
            TEST_ASSERT_INT_EQ(0, entry.encoder_running);
            TEST_ASSERT_INT_EQ(0, (int)status.active_encoders);
            TEST_ASSERT_INT_EQ(0, (int)manager.sessions[0].encoder.frames_written);
            TEST_ASSERT_INT_EQ(1, manager.sessions[0].reconnect_frame.data == NULL);
        }
        now_ms += DF_GVS_MONITOR_REQUEST_INTERVAL_MS;
    }

    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(1, session != NULL);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_STOPPING,
        session == NULL ? -1 : (int)session->state);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, now_ms));
    TEST_ASSERT_INT_EQ(0x02, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(1, (int)trace.last_control_payload_length);

    manager_assert_current_diagnostic(&manager, now_ms + 100U,
        DF_MEDIA_FAILURE_CAPACITY_BUSY, DF_MEDIA_TIMER_RECONNECT, 900U);

    frame.opcode = 0x82U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[0].ipv4, now_ms + 1U));
    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_REQUESTING,
        session == NULL ? -1 : (int)session->state);
    TEST_ASSERT_INT_EQ(1, session != NULL &&
        session->media_generation != generation);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, now_ms + 1U));
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_acknowledges_short_preview_and_retries(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t preview_end[] = {0x00U};
    const uint8_t late_fragment[] = {0xffU, 0xd8U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {
        .family = 0x03U,
        .opcode = 0x02U,
        .payload = preview_end,
        .payload_length = sizeof(preview_end),
    };
    struct df_gvs_video_packet packet = {
        .frame_no = 7U,
        .chunk_count = 2U,
        .chunk_index = 0U,
        .chunk_length = sizeof(late_fragment),
        .capacity = 3U,
        .full_length = 5U,
        .payload = late_fragment,
    };
    struct df_media_session *session;
    uint64_t generation = 0U;
    unsigned controls_before_end;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 100U));
    session = &manager.sessions[0];
    session->monitor.state = DF_GVS_MONITOR_PUBLISHING;
    session->monitor.media_ready = true;
    session->monitor.last_now_ms = 103U;
    session->state = DF_MEDIA_SESSION_PUBLISHING;
    session->frames_received = 1U;
    session->last_frame_ms = 102U;
    session->viewer_active = true;
    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    memcpy(packet.source, stations[0].logical_address, sizeof(packet.source));
    memcpy(packet.destination, local, sizeof(packet.destination));
    controls_before_end = trace.control_count;

    trace.fail_control = true;
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[0].ipv4, 200U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_PUBLISHING, session->monitor.state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PUBLISHING, session->state);
    trace.fail_control = false;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[0].ipv4, 201U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x82, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(0, (int)trace.last_control_payload_length);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, session->monitor.state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_REQUESTING, session->state);
    TEST_ASSERT_INT_EQ((int)generation, (int)session->generation);
    TEST_ASSERT_INT_EQ((int)(generation + 1U), (int)session->media_generation);
    TEST_ASSERT_INT_EQ(0, (int)session->frames_received);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_media_encoder_is_running(&session->encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_push_video(&manager, &packet,
            stations[0].ipv4, 301U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 1200U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4201U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 3U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, session->monitor.state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_REQUESTING, session->state);
    TEST_ASSERT_INT_EQ(0, session->queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_media_encoder_is_running(&session->encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_acknowledges_peer_hangup_during_stop(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t hangup[] = {0x00U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    struct df_gvs_frame frame = {0};
    uint64_t generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 100U));
    key.station_id = "gate_main";
    key.generation = generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 101U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_STOPPING, manager.sessions[0].monitor.state);

    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x02U;
    frame.payload = hangup;
    frame.payload_length = sizeof(hangup);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[0].ipv4, 102U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(0x82, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(0, (int)trace.last_control_payload_length);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_preserves_station_stop_cooldown(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    uint64_t generation = 0U;
    unsigned controls_after_stop;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 100U));
    key.station_id = "gate_main";
    key.generation = generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 200U));
    controls_after_stop = trace.control_count;

    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager,
            &(struct df_gvs_frame){
                .source = {0x32U, 2U, 1U, 0U, 2U, 0U},
                .destination = {0x61U, 2U, 1U, 1U, 1U, 1U},
                .family = 0x03U, .opcode = 0x82U},
            stations[0].ipv4, 201U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 202U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4199U));
    TEST_ASSERT_INT_EQ((int)controls_after_stop,
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4200U));
    TEST_ASSERT_INT_EQ((int)(controls_after_stop + 1U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_leaves_call_keepalive_to_call_control(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .ipv4 = 0x01020300U,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {0};

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(&manager,
        "gate_main", 7U, 100U));

    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x51U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 101U));
    TEST_ASSERT_INT_EQ(0, (int)trace.control_count);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_upgrades_preview_for_incoming_call(void) {
    static const uint8_t queued_frame[] = {0xffU, 0xd8U, 0xffU, 0xd9U};
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U}},
        {.id = "gate_garage", .stream_name = "doorfast_gate_garage",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 4U, 0U}},
    };
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    const struct df_media_session *session;
    struct df_media_session_key key;
    uint64_t generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.local[0] = 0x61U;
    config.local[1] = 2U;
    config.local[2] = 1U;
    config.local[3] = 1U;
    config.local[4] = 1U;
    config.local[5] = 1U;
    config.first_frame_timeout_s = 5U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(
        &manager.sessions[0].queue, generation, 64U));
    manager.sessions[0].queue_initialized = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(
        &manager.sessions[0].queue, queued_frame, sizeof(queued_frame),
        generation, 101U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED,
        df_media_session_manager_incoming_call(&manager, "gate_main",
            generation, UINT64_MAX - 4999U));
    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW,
        session == NULL ? -1 : (int)session->purpose);
    TEST_ASSERT_INT_EQ((int)generation,
        session == NULL ? 0 : (int)session->generation);
    manager.sessions[0].frames_received = 1U;
    manager.sessions[0].last_frame_ms = 100U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
        &manager, "gate_main", generation, 500U));
    session = df_media_session_manager_find(&manager, "gate_main");
    TEST_ASSERT_INT_EQ(1, session != NULL);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL,
        session == NULL ? -1 : (int)session->purpose);
    TEST_ASSERT_INT_EQ(1, session != NULL &&
        session->generation != generation);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(session == NULL ? 0 : (int)session->generation,
        session == NULL ? 0 : (int)session->queue.generation);
    TEST_ASSERT_INT_EQ(0, session == NULL ? -1 : (int)session->queue.count);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO,
        session == NULL ? -1 : (int)session->monitor.state);
    key.station_id = "gate_main";
    key.generation = session == NULL ? 0U : session->generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 501U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 5200U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
        &manager, "gate_main", generation + 1U, 5201U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_incoming_call(&manager, "gate_main",
            generation, 5202U));
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_preempts_oldest_preview_for_call(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U}},
        {.id = "gate_garage", .stream_name = "doorfast_gate_garage",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 4U, 0U}},
    };
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    const struct df_media_session *call;
    uint64_t first_generation = 0U;
    uint64_t second_generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_CALL, 100U, &first_generation));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_start(&manager,
        "gate_side", DF_MEDIA_SESSION_CALL, 200U, &second_generation));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_incoming_call(&manager, "gate_garage",
            44U, 500U));
    config.incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW;
    df_media_session_manager_destroy(&manager);

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW;
    config.local[0] = 0x61U;
    config.local[1] = 2U;
    config.local[2] = 1U;
    config.local[3] = 1U;
    config.local[4] = 1U;
    config.local[5] = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &first_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_side", DF_MEDIA_SESSION_PREVIEW, 200U, &second_generation));
    trace.available_kib = 100U;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RESOURCE_EXHAUSTED,
        df_media_session_manager_incoming_call(&manager, "gate_garage",
            44U, 499U));
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_find(&manager, "gate_main") != NULL);
    TEST_ASSERT_INT_EQ(0, manager.preempted_station_id[0]);
    trace.available_kib = 4096U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_incoming_call(&manager, "gate_garage",
            1U, 500U));
    TEST_ASSERT_INT_EQ(1, strcmp("gate_main",
        manager.preempted_station_id) == 0);
    {
        struct df_media_session_diagnostics_v2 current[2], last[3];
        struct df_media_module_diagnostics_v2 diagnostics = {
            .sessions = current, .session_count = 2U,
            .last_results = last, .last_result_count = 3U,
        };
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
            &manager, 500U, &diagnostics));
        TEST_ASSERT_INT_EQ(2, diagnostics.session_count);
        TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
        TEST_ASSERT_INT_EQ(first_generation, last[0].generation);
        TEST_ASSERT_INT_EQ(0, strcmp("gate_main", last[0].station_id));
        TEST_ASSERT_INT_EQ(DF_MEDIA_END_PREEMPTED, last[0].end_reason);
        TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_PREEMPTED, last[0].failure_reason);
    }
    TEST_ASSERT_INT_EQ((int)first_generation,
        (int)manager.preempted_generation);
    call = df_media_session_manager_find(&manager, "gate_garage");
    TEST_ASSERT_INT_EQ(1, call != NULL);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL,
        call == NULL ? -1 : (int)call->purpose);
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(0x02, trace.last_control_opcode);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_correlates_stop_ack_and_keeps_preview_retrying(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U},
         .ipv4 = 0x01020305U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    struct df_gvs_frame frame = {0};
    uint64_t generation = 0U;
    unsigned attempt;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    key.station_id = "gate_main";
    key.generation = generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 101U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(0x02, trace.last_control_opcode);

    memcpy(frame.destination, local, sizeof(frame.destination));
    memcpy(frame.source, stations[1].logical_address, sizeof(frame.source));
    frame.family = 0x03U;
    frame.opcode = 0x82U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[1].ipv4, 102U));
    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            stations[0].ipv4, 102U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    df_media_session_manager_destroy(&manager);

    memset(&manager, 0, sizeof(manager));
    memset(&trace, 0, sizeof(trace));
    trace.available_kib = 4096U;
    callbacks.context = &trace;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 200U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 200U));
    for (attempt = 1U; attempt < DF_GVS_MONITOR_MAX_REQUESTS; attempt++) {
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
            200U + (uint64_t)attempt *
            DF_GVS_MONITOR_REQUEST_INTERVAL_MS));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
        200U + (uint64_t)DF_GVS_MONITOR_MAX_REQUESTS *
        DF_GVS_MONITOR_REQUEST_INTERVAL_MS));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_retries_stalled_video_session(void) {
    static const uint8_t jpeg[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x00U, 0x00U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x01U, 0xe0U, 0x02U,
        0x80U, 0x00U, 0xffU, 0xd9U,
    };
    static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    static const uint8_t preview_status[] = {0x01U};
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
    };
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks resource_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame frame = {
        .family = 0x03U,
        .opcode = 0x84U,
        .payload_length = sizeof(confirmation),
        .payload = confirmation,
    };
    struct df_media_session_key key;
    struct df_media_session *session;
    uint64_t first_generation = 0U;
    unsigned controls_before_stall;
    unsigned controls_before_end;
    int original_fd;
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {
        .sessions = &entry, .session_count = 1U,
    };
    struct df_gvs_video_packet partial = {
        .frame_no = 55U, .chunk_count = 2U, .chunk_index = 1U,
        .chunk_length = 10U, .capacity = 10U, .full_length = sizeof(jpeg),
        .payload = jpeg,
    };

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 1U;
    memcpy(config.local, local, sizeof(config.local));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &resource_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &first_generation));
    session = &manager.sessions[0];
    memcpy(frame.source, session->station, sizeof(frame.source));
    memcpy(frame.destination, config.local, sizeof(frame.destination));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            session->station_ipv4, 101U));

    session->encoder.input_fd = open("/dev/null", O_WRONLY);
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = first_generation;
    original_fd = session->encoder.input_fd;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4, jpeg,
        sizeof(jpeg), 640U, 480U, 200U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    key.station_id = "gate_main";
    key.generation = first_generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 201U));
    memcpy(partial.source, session->station, sizeof(partial.source));
    memcpy(partial.destination, config.local, sizeof(partial.destination));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 202U));
    TEST_ASSERT_INT_EQ(1, session->video.buffer != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&session->queue,
        jpeg, sizeof(jpeg), first_generation, 202U));
    controls_before_end = trace.control_count;
    frame.opcode = 0x02U;
    frame.payload = preview_status;
    frame.payload_length = sizeof(preview_status);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, session->station_ipv4, 300U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, session->state);
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 1U), (int)trace.control_count);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(original_fd, session->encoder.input_fd);
    TEST_ASSERT_INT_EQ(1, session->queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    TEST_ASSERT_INT_EQ(0, trace.resource_stops);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.encoder_running ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, (int)status.active_encoders);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
    TEST_ASSERT_INT_EQ(2, (int)session->encoder.frames_written);
    TEST_ASSERT_INT_EQ(1, (int)session->frames_received);
    TEST_ASSERT_INT_EQ(200, (int)session->last_frame_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 301U));
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    /* Viewing intent does not interrupt the retained publication. */
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 302U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, false, 302U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PUBLISHING, session->state);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 302U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4299U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 1U), (int)trace.control_count);
    trace.fail_control = true;
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_media_session_manager_tick(&manager, 4300U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U), (int)trace.control_count);
    TEST_ASSERT_INT_EQ(1, session->monitor.retry_waiting ? 1 : 0);
    frame.opcode = 0x84U;
    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            session->station_ipv4, 4300U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 4300U));
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_push_jpeg(
        &manager, session->station, config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 4300U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4301U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U), (int)trace.control_count);
    trace.fail_control = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 5299U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 2U), (int)trace.control_count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 5300U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 3U), (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, session->monitor.state);
    TEST_ASSERT_INT_EQ(1, df_media_encoder_is_running(&session->encoder) ? 1 : 0);
    manager_assert_current_diagnostic(&manager, 5300U,
        DF_MEDIA_FAILURE_NONE, DF_MEDIA_TIMER_NONE, 0U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_push_jpeg(
        &manager, session->station, config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 5301U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 5301U));
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    /* Repeated notifications neither ACK nor reset the retry schedule. */
    frame.opcode = 0x02U;
    frame.payload = preview_status;
    frame.payload_length = sizeof(preview_status);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, session->station_ipv4, 5302U));
    TEST_ASSERT_INT_EQ((int)(controls_before_end + 3U), (int)trace.control_count);
    TEST_ASSERT_INT_EQ((int)(first_generation + 1U), (int)session->media_generation);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, df_media_encoder_is_running(&session->encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 5302U));
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    frame.opcode = 0x84U;
    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, session->station_ipv4, 5302U));
    TEST_ASSERT_INT_EQ(original_fd, session->encoder.input_fd);
    TEST_ASSERT_INT_EQ((int)first_generation, (int)session->generation);
    TEST_ASSERT_INT_EQ((int)(first_generation + 1U),
        (int)session->media_generation);
    TEST_ASSERT_INT_EQ((int)first_generation, (int)session->encoder.generation);
    /* The replacement station starts numbering again at one. */
    partial.frame_no = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 5399U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    partial.chunk_index = 2U;
    partial.payload = jpeg + 10U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &partial, session->station_ipv4, 5400U));
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);

    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 8199U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4, jpeg,
        sizeof(jpeg), 640U, 480U, 8199U));
    controls_before_stall = trace.control_count;
    /* Without an explicit peer end, retain the existing stall fallback. */
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 30198U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 38198U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 38199U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1,
        df_media_encoder_is_running(&session->encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, session->state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_STOPPING, session->monitor.state);
    TEST_ASSERT_INT_EQ(0, trace.resource_stops);
    TEST_ASSERT_INT_EQ((int)(controls_before_stall + 2U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 38200U));
    TEST_ASSERT_INT_EQ((int)(controls_before_stall + 3U),
        (int)trace.control_count);
    TEST_ASSERT_INT_EQ(0x03, trace.last_control_family);
    TEST_ASSERT_INT_EQ(0x02, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(0,
        memcmp(trace.last_control_destination, stations[0].logical_address,
            sizeof(trace.last_control_destination)));
    manager_assert_current_diagnostic(&manager, 38300U,
        DF_MEDIA_FAILURE_SOURCE_STALLED, DF_MEDIA_TIMER_RECONNECT, 900U);
    TEST_ASSERT_INT_EQ(0,
        memcmp(trace.last_control_source, config.local,
            sizeof(trace.last_control_source)));
    frame.opcode = 0x82U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, stations[0].ipv4, 38201U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, session->state);
    TEST_ASSERT_INT_EQ((int)(first_generation + 2U),
        (int)session->media_generation);
    TEST_ASSERT_INT_EQ(1, session->viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 38201U));
    TEST_ASSERT_INT_EQ(0x04, trace.last_control_opcode);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    key.station_id = "gate_main";
    key.generation = first_generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 38201U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    frame.opcode = 0x82U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, stations[0].ipv4, 38202U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));

    df_media_session_manager_destroy(&manager);

    memset(&manager, 0, sizeof(manager));
    memset(&trace, 0, sizeof(trace));
    trace.available_kib = 4096U;
    callbacks.context = &trace;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 40000U, &first_generation));
    session = &manager.sessions[0];
    session->monitor.state = DF_GVS_MONITOR_PUBLISHING;
    session->monitor.media_ready = true;
    session->monitor.last_now_ms = 40000U;
    session->state = DF_MEDIA_SESSION_PUBLISHING;
    session->frames_received = 1U;
    session->last_frame_ms = 40000U;
    trace.fail_control = true;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_tick(&manager, 70001U));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_media_session_manager_tick(&manager, 70002U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_VIDEO_STALLED, manager.failed_error);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_reports_ready_only_after_media_flows(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {
        .sessions = &entry,
        .session_count = 1U,
    };
    struct df_media_session_key key = {.station_id = "gate_main"};
    struct df_media_session *session;
    uint64_t revision_before_frame;
    uint64_t revision_after_frame;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &key.generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 101U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, entry.encoder_running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, entry.ready ? 1 : 0);
    revision_before_frame = entry.status_revision;

    session = &manager.sessions[0];
    session->frames_received = 1U;
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, entry.encoder_running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > revision_before_frame);
    revision_after_frame = entry.status_revision;

    session->encoder.input_fd = open("/dev/null", O_WRONLY);
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = key.generation;
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.encoder_running ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > revision_after_frame);

    session->encoder.running = false;
    status.session_count = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, entry.ready ? 1 : 0);

    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_preserves_previews_when_policy_requests_it(void) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U}},
        {.id = "gate_garage", .stream_name = "doorfast_gate_garage",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 4U, 0U}},
    };
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    uint64_t generation = 0U;

    manager_fixture(&config, &callbacks, &trace);
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.incoming_call_policy = DF_MEDIA_CALL_PRESERVE_PREVIEWS;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_side", DF_MEDIA_SESSION_PREVIEW, 200U, &generation));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY,
        df_media_session_manager_incoming_call(&manager, "gate_garage",
            44U, 500U));
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_find(&manager,
        "gate_garage") == NULL);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_snapshots_cleanup_per_session(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace first_trace = {.available_kib = 4096U};
    struct manager_trace second_trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks first_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &first_trace,
    };
    const struct df_media_session_resource_hooks second_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &second_trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_media_session_key first_key;
    uint64_t first_generation = 0U;
    uint64_t second_generation = 0U;

    manager_fixture(&config, &callbacks, &first_trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &first_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 400U,
        &first_generation));
    df_media_session_manager_set_resource_hooks(&manager, &second_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_side", DF_MEDIA_SESSION_PREVIEW, 401U,
        &second_generation));
    TEST_ASSERT_INT_EQ(1, first_trace.resource_starts);
    TEST_ASSERT_INT_EQ(1, second_trace.resource_starts);

    first_key.station_id = "gate_main";
    first_key.generation = first_generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &first_key, false, 402U));
    TEST_ASSERT_INT_EQ(1, first_trace.resource_stops);
    TEST_ASSERT_INT_EQ(0, second_trace.resource_stops);

    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(1, first_trace.resource_stops);
    TEST_ASSERT_INT_EQ(1, second_trace.resource_stops);
}

void test_media_session_manager_rejects_reinit_without_losing_owner_state(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks resource_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    uint64_t generation = 0U;
    uint64_t next_generation;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &resource_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 500U, &generation));
    next_generation = manager.next_generation;
    key.station_id = "gate_main";
    key.generation = generation;

    config.max_encoders = 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(2, df_media_session_manager_capacity(&manager));
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(next_generation, manager.next_generation);
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &key) != NULL);

    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(1, trace.resource_stops);
}

void test_media_session_manager_generation_limit_is_failure_atomic(void) {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks resource_hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key;
    uint64_t first_generation = 0U;
    uint64_t rejected_generation = 99U;
    unsigned starts_before;
    unsigned stops_before;

    manager_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &resource_hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
        &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 600U,
        &first_generation));
    key.station_id = "gate_main";
    key.generation = first_generation;
    starts_before = trace.resource_starts;
    stops_before = trace.resource_stops;
    manager.next_generation = UINT64_MAX;

    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RESOURCE_EXHAUSTED,
        df_media_session_manager_start(&manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 601U, &rejected_generation));
    TEST_ASSERT_INT_EQ(0, rejected_generation);
    TEST_ASSERT_INT_EQ(starts_before, trace.resource_starts);
    TEST_ASSERT_INT_EQ(stops_before, trace.resource_stops);
    TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &key) != NULL);
    TEST_ASSERT_INT_EQ(1, manager.next_generation == UINT64_MAX);
    df_media_session_manager_destroy(&manager);
}

static int manager_write_ffmpeg_capture(char directory[], char program[],
    size_t program_capacity) {
    static const char script[] =
        "#!/bin/sh\n"
        "destination=\n"
        "for argument; do destination=$argument; done\n"
        "stream=${destination##*/}\n"
        "printf '%s\\n' \"$destination\" >> \"$DF_TEST_MEDIA_CAPTURE_DIR/destinations\"\n"
        "exec /bin/cat >> \"$DF_TEST_MEDIA_CAPTURE_DIR/$stream\"\n";
    int descriptor;

    if (mkdtemp(directory) == NULL ||
        snprintf(program, program_capacity, "%s/ffmpeg", directory) >=
            (int)program_capacity) return DF_ERR_IO;
    descriptor = open(program, O_WRONLY | O_CREAT | O_EXCL, 0700);
    if (descriptor < 0 || write(descriptor, script, sizeof(script) - 1U) !=
            (ssize_t)(sizeof(script) - 1U) || close(descriptor) != 0) {
        if (descriptor >= 0) (void)close(descriptor);
        (void)unlink(program);
        (void)rmdir(directory);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int manager_read_capture(const char *directory, const char *stream,
    uint8_t *contents, size_t capacity) {
    char path[256];
    int descriptor;
    ssize_t length;

    if (snprintf(path, sizeof(path), "%s/%s", directory, stream) >=
        (int)sizeof(path)) return -1;
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    length = read(descriptor, contents, capacity);
    (void)close(descriptor);
    return (int)length;
}

static int manager_wait_for_capture(const char *directory, const char *stream,
    size_t expected_length) {
    char path[256];
    unsigned attempt;

    if (snprintf(path, sizeof(path), "%s/%s", directory, stream) >=
        (int)sizeof(path)) return -1;
    for (attempt = 0U; attempt < 1000U; attempt++) {
        struct stat captured;
        struct timespec delay = {0, 1000000L};

        if (stat(path, &captured) == 0 &&
            (size_t)captured.st_size >= expected_length)
            return (int)captured.st_size;
        (void)nanosleep(&delay, NULL);
    }
    return -1;
}

static void manager_pipeline_fixture(struct df_media_module_config_v3 *,
    struct df_media_module_callbacks_v3 *, struct manager_trace *);

/* Catch producer replacement, lost ready, and accidental publication of old
 * fragments by observing the bytes consumed by the same encoder child. */
static void manager_keeps_producer_across_busy_retry(uint8_t end_reason) {
    static const uint8_t jpeg[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x00U, 0x00U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x01U, 0xe0U, 0x02U,
        0x80U, 0x00U, 0xffU, 0xd9U,
    };
    static const uint8_t confirmation[] = {0x1eU, 0U, 1U};
    const uint8_t ended[] = {end_reason};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session *session;
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {.sessions = &entry, .session_count = 1U};
    struct df_media_session_key key = {.station_id = "gate_main"};
    struct df_gvs_frame control = {.family = 3U, .opcode = 0x84U,
        .payload = confirmation, .payload_length = sizeof(confirmation)};
    struct df_gvs_video_packet fragment = {.frame_no = 55U,
        .chunk_count = 2U, .chunk_index = 1U, .chunk_length = 10U,
        .capacity = 10U, .full_length = sizeof(jpeg), .payload = jpeg};
    char directory[] = "/tmp/doorfast-continuity-XXXXXX";
    char program[256], capture_path[256], saved_path[4096];
    const char *path = getenv("PATH");
    uint8_t *captured;
    size_t prompt_length, expected_length;
    uint64_t revision;
    pid_t original_pid;

    TEST_ASSERT_INT_EQ(1, path != NULL && strlen(path) < sizeof(saved_path));
    if (path == NULL || strlen(path) >= sizeof(saved_path)) return;
    strcpy(saved_path, path);
    TEST_ASSERT_INT_EQ(DF_OK, manager_write_ffmpeg_capture(directory, program, sizeof(program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", directory, 1));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_CAPTURE_DIR", directory, 1));
    manager_pipeline_fixture(&config, &callbacks, &trace);
    config.station_count = 1U;
    config.max_encoders = 1U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &key.generation));
    session = &manager.sessions[0];
    memcpy(control.source, session->station, sizeof(control.source));
    memcpy(control.destination, config.local, sizeof(control.destination));
    memcpy(fragment.source, session->station, sizeof(fragment.source));
    memcpy(fragment.destination, config.local, sizeof(fragment.destination));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 102U));
    original_pid = session->encoder.pid;
    TEST_ASSERT_INT_EQ(1, original_pid > 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    revision = entry.status_revision;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &fragment, session->station_ipv4, 103U));
    control.opcode = 0x02U;
    control.payload = ended;
    control.payload_length = sizeof(ended);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 200U));
    prompt_length = session->reconnect_frame.length;
    if (end_reason == 0U) {
        unsigned controls_after_end = trace.control_count;

        TEST_ASSERT_INT_EQ(0x82, trace.last_control_opcode);
        TEST_ASSERT_INT_EQ(0, (int)trace.last_control_payload_length);
        TEST_ASSERT_INT_EQ((int)(key.generation + 1U), (int)session->media_generation);
        TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
        /* A valid old tail cannot finish its buffered head before 03/84. */
        fragment.chunk_index = 2U;
        fragment.payload = jpeg + 10U;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
            &fragment, session->station_ipv4, 200U));
        TEST_ASSERT_INT_EQ(1, (int)session->frames_received);
        TEST_ASSERT_INT_EQ(102, (int)session->last_frame_ms);
        TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
            &control, session->station_ipv4, 200U));
        TEST_ASSERT_INT_EQ((int)(controls_after_end + 1U), (int)trace.control_count);
        TEST_ASSERT_INT_EQ(0x82, trace.last_control_opcode);
        TEST_ASSERT_INT_EQ((int)(key.generation + 1U), (int)session->media_generation);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready);
    TEST_ASSERT_INT_EQ(0, entry.source_live);
    TEST_ASSERT_INT_EQ(1, entry.encoder_running);
    TEST_ASSERT_INT_EQ(1, (int)status.active_encoders);
    TEST_ASSERT_INT_EQ(1, entry.status_revision > revision);
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_gate_main", entry.stream_name));
    TEST_ASSERT_INT_EQ((int)key.generation, (int)entry.generation);
    TEST_ASSERT_INT_EQ((int)original_pid, (int)session->encoder.pid);
    TEST_ASSERT_INT_EQ(1, session->video.buffer == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 4200U));
    control.opcode = 0x50U;
    control.payload = NULL;
    control.payload_length = 0U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 4201U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 5200U));
    control.opcode = 0x84U;
    control.payload = confirmation;
    control.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 5201U));
    /* Old first half must not combine with this new attempt's second half. */
    fragment.chunk_index = 2U;
    fragment.payload = jpeg + 10U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &fragment, session->station_ipv4, 5202U));
    TEST_ASSERT_INT_EQ(1, (int)session->frames_received);
    fragment.chunk_index = 1U;
    fragment.payload = jpeg;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(&manager,
        &fragment, session->station_ipv4, 5203U));
    TEST_ASSERT_INT_EQ(2, (int)session->frames_received);
    TEST_ASSERT_INT_EQ(5203, (int)session->last_frame_ms);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_LIVE, session->source_state);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.source_live);
    TEST_ASSERT_INT_EQ((int)(key.generation + 1U), (int)session->media_generation);
    TEST_ASSERT_INT_EQ((int)key.generation, (int)session->encoder.generation);
    TEST_ASSERT_INT_EQ((int)original_pid, (int)session->encoder.pid);
    expected_length = 2U * sizeof(jpeg) + 3U * prompt_length;
    TEST_ASSERT_INT_EQ((int)expected_length,
        manager_wait_for_capture(directory, "doorfast_gate_main", expected_length));
    captured = malloc(expected_length);
    TEST_ASSERT_INT_EQ(1, captured != NULL);
    if (captured != NULL) {
        int length = manager_read_capture(directory, "doorfast_gate_main", captured, expected_length);
        TEST_ASSERT_INT_EQ((int)expected_length, length);
        if (length == (int)expected_length) {
            TEST_ASSERT_INT_EQ(0, memcmp(jpeg, captured, sizeof(jpeg)));
            TEST_ASSERT_INT_EQ(0, memcmp(session->reconnect_frame.data,
                captured + sizeof(jpeg), prompt_length));
            TEST_ASSERT_INT_EQ(0, memcmp(jpeg,
                captured + expected_length - sizeof(jpeg), sizeof(jpeg)));
        }
        free(captured);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 5300U));
    TEST_ASSERT_INT_EQ(0, df_media_encoder_is_running(&session->encoder));
    TEST_ASSERT_INT_EQ(1, session->reconnect_frame.data == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, entry.ready);
    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(0, setenv("PATH", saved_path, 1));
    TEST_ASSERT_INT_EQ(0, unsetenv("DF_TEST_MEDIA_CAPTURE_DIR"));
    (void)snprintf(capture_path, sizeof(capture_path), "%s/doorfast_gate_main", directory);
    (void)unlink(capture_path);
    (void)snprintf(capture_path, sizeof(capture_path), "%s/destinations", directory);
    (void)unlink(capture_path);
    (void)unlink(program);
    (void)rmdir(directory);
}

void test_media_session_manager_keeps_producer_across_busy_retry(void) {
    manager_keeps_producer_across_busy_retry(1U);
}

void test_media_session_manager_keeps_producer_after_acknowledged_peer_end(void) {
    manager_keeps_producer_across_busy_retry(0U);
}

static void manager_pipeline_fixture(struct df_media_module_config_v3 *config,
    struct df_media_module_callbacks_v3 *callbacks,
    struct manager_trace *trace) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = 0x01020304U},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U},
         .ipv4 = 0x01020305U},
    };

    manager_fixture(config, callbacks, trace);
    config->local[0] = 0x61U;
    config->local[1] = 2U;
    config->local[2] = 1U;
    config->local[3] = 1U;
    config->local[4] = 1U;
    config->local[5] = 1U;
    config->stations = stations;
    config->station_count = sizeof(stations) / sizeof(stations[0]);
    config->go2rtc_host = "127.0.0.1";
    config->go2rtc_port = 8554U;
    config->rtsp_username = "doorfast";
    config->credentials_path = "/tmp/doorfast-task3-missing-credentials";
    config->encoder = DF_MEDIA_ENCODER_SOFTWARE;
    config->resolution = DF_MEDIA_RESOLUTION_SOURCE;
    config->fps = 10U;
    config->bitrate_kbps = 800U;
    config->profile = DF_MEDIA_PROFILE_BASELINE;
    config->min_free_kib = 0U;
}

/* waitpid can report a live child even after its input reader has closed. */
void test_media_session_manager_recovers_live_child_epipe(void) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    char directory[] = "/tmp/doorfast-epipe-recovery-XXXXXX";
    char program[256], capture_path[256], saved_path[4096];
    const char *path = getenv("PATH");
    unsigned scenario;

    TEST_ASSERT_INT_EQ(1, path != NULL && strlen(path) < sizeof(saved_path));
    if (path == NULL || strlen(path) >= sizeof(saved_path)) return;
    strcpy(saved_path, path);
    TEST_ASSERT_INT_EQ(DF_OK, manager_write_ffmpeg_capture(directory, program, sizeof(program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", directory, 1));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_CAPTURE_DIR", directory, 1));
    for (scenario = 0U; scenario < 2U; scenario++) {
        struct df_media_module_config_v3 config;
        struct df_media_module_callbacks_v3 callbacks;
        struct manager_trace trace = {.available_kib = 4096U};
        struct df_media_session_manager manager = {0};
        struct df_media_session *session;
        struct df_media_session_key key = {.station_id = "gate_main"};
        struct df_media_session_status_v3 entry = {0};
        struct df_media_module_status_v3 status = {.sessions = &entry, .session_count = 1U};
        int input[2], handshake[2];
        uint8_t bytes[4096] = {0};
        pid_t child;

        manager_pipeline_fixture(&config, &callbacks, &trace);
        config.local[0] = 0U;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
            "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &key.generation));
        TEST_ASSERT_INT_EQ(0, pipe(input));
        TEST_ASSERT_INT_EQ(0, fcntl(input[1], F_SETFL, O_NONBLOCK));
        session = &manager.sessions[0];
        session->encoder.input_fd = input[1];
        session->encoder.input_owned = true;
        session->encoder.running = true;
        session->encoder.generation = key.generation;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
            session->station, config.local, session->station_ipv4,
            jpeg, sizeof(jpeg), 640U, 480U, 200U));
        TEST_ASSERT_INT_EQ((int)sizeof(jpeg), (int)read(input[0], bytes, sizeof(bytes)));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 201U));
        if (scenario == 0U) {
            /* LIVE: a real frame remains pending behind a full pipe. */
            while (write(input[1], bytes, sizeof(bytes)) > 0) {}
            TEST_ASSERT_INT_EQ(1, errno == EAGAIN || errno == EWOULDBLOCK);
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
                session->station, config.local, session->station_ipv4,
                jpeg, sizeof(jpeg), 640U, 480U, 202U));
            TEST_ASSERT_INT_EQ(1, session->encoder.pending_frame != NULL);
        } else {
            /* RECONNECTING: the next write is a hint, without pending input. */
            df_media_session_mark_source_lost(session, 202U);
            TEST_ASSERT_INT_EQ(1, session->encoder.pending_frame == NULL);
        }
        TEST_ASSERT_INT_EQ(0, pipe(handshake));
        child = fork();
        if (child == 0) {
            (void)close(input[0]);
            (void)close(input[1]);
            (void)close(handshake[0]);
            (void)write(handshake[1], "x", 1U);
            (void)close(handshake[1]);
            for (;;) pause();
        }
        TEST_ASSERT_INT_EQ(1, child > 0);
        (void)close(handshake[1]);
        TEST_ASSERT_INT_EQ(1, (int)read(handshake[0], bytes, 1U));
        (void)close(handshake[0]);
        (void)close(input[0]);
        session->encoder.pid = child;
        TEST_ASSERT_INT_EQ(0, (int)waitpid(child, NULL, WNOHANG));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
        TEST_ASSERT_INT_EQ(-1, (int)waitpid(child, NULL, WNOHANG));
        TEST_ASSERT_INT_EQ(ECHILD, errno);
        TEST_ASSERT_INT_EQ(1, (int)df_media_session_manager_active(&manager));
        TEST_ASSERT_INT_EQ((int)key.generation, (int)session->generation);
        TEST_ASSERT_INT_EQ(1, session->viewer_active);
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_NONE, session->last_error);
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_NONE, manager.failed_error);
        TEST_ASSERT_INT_EQ(1, session->encoder.pending_frame == NULL);
        TEST_ASSERT_INT_EQ(1, session->reconnect_frame.data == NULL);
        TEST_ASSERT_INT_EQ(0, (int)session->publication_generation);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        TEST_ASSERT_INT_EQ(1, (int)status.session_count);
        TEST_ASSERT_INT_EQ(0, entry.ready);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
            session->station, config.local, session->station_ipv4,
            jpeg, sizeof(jpeg), 640U, 480U, 301U));
        status.session_count = 1U;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        TEST_ASSERT_INT_EQ(1, entry.ready);
        TEST_ASSERT_INT_EQ(1, session->encoder.pid > 0 && session->encoder.pid != child);
        TEST_ASSERT_INT_EQ((int)((scenario + 1U) * sizeof(jpeg)),
            manager_wait_for_capture(directory, "doorfast_gate_main",
                (scenario + 1U) * sizeof(jpeg)));
        df_media_session_manager_destroy(&manager);
    }
    TEST_ASSERT_INT_EQ(0, setenv("PATH", saved_path, 1));
    TEST_ASSERT_INT_EQ(0, unsetenv("DF_TEST_MEDIA_CAPTURE_DIR"));
    (void)snprintf(capture_path, sizeof(capture_path), "%s/doorfast_gate_main", directory);
    (void)unlink(capture_path);
    (void)snprintf(capture_path, sizeof(capture_path), "%s/destinations", directory);
    (void)unlink(capture_path);
    (void)unlink(program);
    (void)rmdir(directory);
}

/* A call takes over its station while other reconnecting publications live. */
void test_media_session_manager_keeps_three_reconnecting_stations_isolated(void) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_status_v3 entries[3];
    struct df_media_module_status_v3 status = {.sessions = entries, .session_count = 3U};
    uint64_t generations[3];
    int inputs[3];
    size_t index;

    manager_fixture(&config, &callbacks, &trace);
    config.max_encoders = 3U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
    for (index = 0U; index < 3U; index++) {
        struct df_media_session *session;

        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
            config.stations[index].id, DF_MEDIA_SESSION_PREVIEW, 100U,
            &generations[index]));
        session = &manager.sessions[index];
        inputs[index] = open("/dev/null", O_WRONLY);
        TEST_ASSERT_INT_EQ(1, inputs[index] >= 0);
        session->encoder.input_fd = inputs[index];
        session->encoder.input_owned = true;
        session->encoder.running = true;
        session->encoder.generation = generations[index];
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
            session->station, config.local, session->station_ipv4,
            jpeg, sizeof(jpeg), 640U, 480U, 200U));
        df_media_session_mark_source_lost(session, 300U);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(3, (int)status.active_encoders);
    for (index = 0U; index < 3U; index++) {
        TEST_ASSERT_INT_EQ(1, entries[index].ready);
        TEST_ASSERT_INT_EQ((int)generations[index], (int)entries[index].generation);
        TEST_ASSERT_INT_EQ(inputs[index], manager.sessions[index].encoder.input_fd);
        TEST_ASSERT_INT_EQ(2, (int)manager.sessions[index].encoder.frames_written);
        TEST_ASSERT_INT_EQ(1, (int)manager.sessions[index].frames_received);
        TEST_ASSERT_INT_EQ(200, (int)manager.sessions[index].last_frame_ms);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(&manager,
        "gate_main", 70U, 301U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 400U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL, entries[0].purpose);
    TEST_ASSERT_INT_EQ(0, entries[0].ready);
    TEST_ASSERT_INT_EQ(0, entries[0].encoder_running);
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].reconnect_frame.data == NULL);
    TEST_ASSERT_INT_EQ(0, (int)manager.sessions[0].publication_generation);
    TEST_ASSERT_INT_EQ(1, entries[0].generation != generations[0]);
    TEST_ASSERT_INT_EQ(2, (int)status.active_encoders);
    for (index = 1U; index < 3U; index++) {
        struct df_media_session_key key = {
            .station_id = config.stations[index].id,
            .generation = generations[index],
        };

        TEST_ASSERT_INT_EQ(1, entries[index].ready);
        TEST_ASSERT_INT_EQ(inputs[index], manager.sessions[index].encoder.input_fd);
        TEST_ASSERT_INT_EQ(3, (int)manager.sessions[index].encoder.frames_written);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 401U));
        TEST_ASSERT_INT_EQ(1, manager.sessions[index].reconnect_frame.data == NULL);
        TEST_ASSERT_INT_EQ(0, (int)manager.sessions[index].publication_generation);
        TEST_ASSERT_INT_EQ(0, (int)manager.sessions[index].generation);
        status.session_count = 3U;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        TEST_ASSERT_INT_EQ((int)(3U - index), (int)status.required_session_count);
        TEST_ASSERT_INT_EQ((int)(2U - index), (int)status.active_encoders);
        TEST_ASSERT_INT_EQ(1,
            df_media_session_manager_lookup(&manager, &key) == NULL);
    }
    df_media_session_manager_destroy(&manager);
}

/* A completed old frame must never follow the reconnect image. The encoder's
 * pending copy still belongs to its original timestamp and must finish first. */
static void manager_backpressure_attempt_boundary(void) {
    static const uint8_t a[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    static const uint8_t b[] = {0xffU, 0xd8U, 0x22U, 0xffU, 0xd9U};
    static const uint8_t confirmation[] = {0x1eU, 0U, 1U};
    static const uint8_t ended[] = {0U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_gvs_frame control = {.family = 3U, .opcode = 0x84U,
        .payload = confirmation, .payload_length = sizeof(confirmation)};
    struct df_media_session *session;
    uint64_t generation;
    int descriptors[2];
    uint8_t filler[4096] = {0};
    uint8_t *captured, *pending;
    size_t length, used = 0U;
    unsigned tick;
    ssize_t count;

    manager_pipeline_fixture(&config, &callbacks, &trace);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    session = &manager.sessions[0];
    memcpy(control.source, session->station, 6U);
    memcpy(control.destination, config.local, 6U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 101U));
    TEST_ASSERT_INT_EQ(0, pipe(descriptors));
    TEST_ASSERT_INT_EQ(0, fcntl(descriptors[0], F_SETFL, O_NONBLOCK));
    TEST_ASSERT_INT_EQ(0, fcntl(descriptors[1], F_SETFL, O_NONBLOCK));
    session->encoder.input_fd = descriptors[1];
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = generation;
    while (write(descriptors[1], filler, sizeof(filler)) > 0) {}
    TEST_ASSERT_INT_EQ(1, errno == EAGAIN || errno == EWOULDBLOCK);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4,
        a, sizeof(a), 640U, 480U, 200U));
    pending = session->encoder.pending_frame;
    TEST_ASSERT_INT_EQ(1, pending != NULL);
    control.opcode = 2U;
    control.payload = ended;
    control.payload_length = sizeof(ended);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 300U));
    TEST_ASSERT_INT_EQ(0, (int)session->queue.count);
    TEST_ASSERT_INT_EQ(1, pending == session->encoder.pending_frame);
    TEST_ASSERT_INT_EQ(200, (int)session->encoder.pending_timestamp_ms);
    TEST_ASSERT_INT_EQ((int)generation, (int)session->encoder.pending_generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
    TEST_ASSERT_INT_EQ(200, (int)session->encoder.pending_timestamp_ms);
    while (read(descriptors[0], filler, sizeof(filler)) > 0) {}
    length = sizeof(a) + session->reconnect_frame.length + sizeof(b);
    captured = malloc(length);
    TEST_ASSERT_INT_EQ(1, captured != NULL);
    if (captured == NULL) {
        df_media_session_manager_destroy(&manager);
        (void)close(descriptors[0]);
        return;
    }
    /* Fixed time prevents a second prompt while a large JPEG drains. */
    for (tick = 0U; tick < 100U && used < length - sizeof(b); tick++) {
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 301U));
        while ((count = read(descriptors[0], captured + used, length - used)) > 0)
            used += (size_t)count;
    }
    TEST_ASSERT_INT_EQ((int)(length - sizeof(b)), (int)used);
    control.opcode = 0x84U;
    control.payload = confirmation;
    control.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &control, session->station_ipv4, 302U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4,
        b, sizeof(b), 640U, 480U, 303U));
    while ((count = read(descriptors[0], captured + used, length - used)) > 0)
        used += (size_t)count;
    TEST_ASSERT_INT_EQ((int)length, (int)used);
    if (used == length) {
        TEST_ASSERT_INT_EQ(0, memcmp(a, captured, sizeof(a)));
        TEST_ASSERT_INT_EQ(0, memcmp(session->reconnect_frame.data,
            captured + sizeof(a), session->reconnect_frame.length));
        TEST_ASSERT_INT_EQ(0, memcmp(b, captured + length - sizeof(b), sizeof(b)));
    }
    TEST_ASSERT_INT_EQ(0, (int)session->queue.count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_LIVE, session->source_state);
    free(captured);
    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(0, close(descriptors[0]));
}

/* A full encoder pipe is a recoverable RETRY, not a lost producer. */
void test_media_session_manager_reconnect_backpressure_is_bounded(void) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session *session;
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {
        .sessions = &entry, .session_count = 1U,
    };
    uint64_t generation;
    int descriptors[2];
    uint8_t bytes[4096] = {0};
    uint8_t *pending;
    size_t pending_length;

    manager_backpressure_attempt_boundary();

    manager_pipeline_fixture(&config, &callbacks, &trace);
    config.local[0] = 0U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
    TEST_ASSERT_INT_EQ(0, pipe(descriptors));
    TEST_ASSERT_INT_EQ(0, fcntl(descriptors[0], F_SETFL, O_NONBLOCK));
    TEST_ASSERT_INT_EQ(0, fcntl(descriptors[1], F_SETFL, O_NONBLOCK));
    session = &manager.sessions[0];
    session->encoder.input_fd = descriptors[1];
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 200U));
    TEST_ASSERT_INT_EQ((int)sizeof(jpeg), (int)read(descriptors[0], bytes, sizeof(bytes)));
    while (write(descriptors[1], bytes, sizeof(bytes)) > 0) {}
    TEST_ASSERT_INT_EQ(1, errno == EAGAIN || errno == EWOULDBLOCK);
    df_media_session_mark_source_lost(session, 300U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
    pending = session->encoder.pending_frame;
    pending_length = session->encoder.pending_length;
    TEST_ASSERT_INT_EQ(1, pending != NULL);
    TEST_ASSERT_INT_EQ((int)session->reconnect_frame.length, (int)pending_length);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 301U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 400U));
    TEST_ASSERT_INT_EQ(1, pending == session->encoder.pending_frame);
    TEST_ASSERT_INT_EQ((int)pending_length, (int)session->encoder.pending_length);
    TEST_ASSERT_INT_EQ(0, (int)session->queue.count);
    TEST_ASSERT_INT_EQ(1, (int)session->frames_received);
    TEST_ASSERT_INT_EQ(200, (int)session->last_frame_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(1, entry.ready);
    TEST_ASSERT_INT_EQ(1, (int)status.active_encoders);
    while (read(descriptors[0], bytes, sizeof(bytes)) > 0) {}
    TEST_ASSERT_INT_EQ(1, errno == EAGAIN || errno == EWOULDBLOCK);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 401U));
    TEST_ASSERT_INT_EQ(1, session->encoder.frames_written >= 2U);
    TEST_ASSERT_INT_EQ(1, session->encoder.pending_length <= pending_length);
    TEST_ASSERT_INT_EQ(1, (int)session->frames_received);
    TEST_ASSERT_INT_EQ(200, (int)session->last_frame_ms);
    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(0, close(descriptors[0]));
}

/* Failures that cannot produce an honest reconnect image must release the
 * publication and report their own error, rather than encoder_failed. */
void test_media_session_manager_reports_reconnect_frame_failure(void) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    unsigned scenario;

    for (scenario = 0U; scenario < 6U; scenario++) {
        struct df_media_module_config_v3 config;
        struct df_media_module_callbacks_v3 callbacks;
        struct manager_trace trace = {.available_kib = 4096U};
        struct df_media_session_manager manager = {0};
        struct df_media_session *session;
        uint64_t generation;
        struct df_media_module_status_v3 status = {0};
        static const uint8_t confirmation[] = {0x1eU, 0U, 1U};
        struct df_gvs_frame control = {.family = 3U, .opcode = 0x84U,
            .payload = confirmation, .payload_length = sizeof(confirmation)};
        unsigned controls_before_failure;

        manager_pipeline_fixture(&config, &callbacks, &trace);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
            "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &generation));
        session = &manager.sessions[0];
        memcpy(control.source, session->station, sizeof(control.source));
        memcpy(control.destination, config.local, sizeof(control.destination));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
            &control, session->station_ipv4, 101U));
        controls_before_failure = trace.control_count;
        trace.fail_control = scenario >= 3U;
        session->encoder.input_fd = open("/dev/null", O_WRONLY);
        TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
        session->encoder.input_owned = true;
        session->encoder.running = true;
        session->encoder.generation = generation;
        if (scenario % 3U == 0U) {
            TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_session_manager_push_jpeg(&manager,
                session->station, config.local, session->station_ipv4,
                jpeg, sizeof(jpeg), 32U, 32U, 200U));
        } else {
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
                session->station, config.local, session->station_ipv4,
                jpeg, sizeof(jpeg), 640U, 480U, 200U));
            df_media_session_mark_source_lost(session, 300U);
            if (scenario % 3U == 1U) {
                TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_session_manager_push_jpeg(&manager,
                    session->station, config.local, session->station_ipv4,
                    jpeg, sizeof(jpeg), 800U, 600U, 301U));
            } else {
                df_media_reconnect_frame_destroy(&session->reconnect_frame);
                TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_session_manager_tick(&manager, 300U));
            }
        }
        TEST_ASSERT_INT_EQ(1,
            trace.control_count >= controls_before_failure + 1U);
        TEST_ASSERT_INT_EQ(3, trace.last_control_family);
        TEST_ASSERT_INT_EQ(2, trace.last_control_opcode);
        TEST_ASSERT_INT_EQ(0, memcmp(control.source, trace.last_control_destination, 6U));
        TEST_ASSERT_INT_EQ(0, memcmp(config.local, trace.last_control_source, 6U));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RECONNECT_FRAME, status.failed_error);
        TEST_ASSERT_INT_EQ((int)generation, (int)status.failed_generation);
        TEST_ASSERT_INT_EQ(0, strcmp("gate_main", status.failed_station_id));
        TEST_ASSERT_INT_EQ(0, (int)status.active_encoders);
        TEST_ASSERT_INT_EQ(0, (int)status.required_session_count);
        TEST_ASSERT_INT_EQ(0, (int)df_media_session_manager_active(&manager));
        TEST_ASSERT_INT_EQ(1, session->reconnect_frame.data == NULL);
        TEST_ASSERT_INT_EQ(0, session->queue_initialized);
        TEST_ASSERT_INT_EQ(0, (int)session->publication_generation);
        TEST_ASSERT_INT_EQ(0, (int)session->generation);
        TEST_ASSERT_INT_EQ(0, (int)session->media_generation);
        df_media_session_manager_destroy(&manager);
    }
}

static void manager_shared_viewer_stops_after_timeout(bool fail_control) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    static const uint8_t confirmation[] = {0x1eU, 0U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    const struct df_media_session_resource_hooks hooks = {
        .start = manager_start_resources,
        .stop = manager_stop_resources,
        .context = &trace,
    };
    struct df_media_session_manager manager = {0};
    struct df_media_session_status_v3 entry;
    struct df_media_module_status_v3 status = {
        .sessions = &entry, .session_count = 1U,
    };
    struct df_media_session_key key = {.station_id = "gate_main"};
    struct df_gvs_frame frame = {
        .family = 3U, .opcode = 0x84U,
        .payload = confirmation, .payload_length = sizeof(confirmation),
    };
    struct df_media_session *session;

    manager_pipeline_fixture(&config, &callbacks, &trace);
    config.go2rtc_host = NULL;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(&manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(&manager, &hooks);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &key.generation));
    session = &manager.sessions[0];
    memcpy(frame.source, session->station, sizeof(frame.source));
    memcpy(frame.destination, config.local, sizeof(frame.destination));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(&manager,
        &frame, session->station_ipv4, 101U));
    session->encoder.input_fd = open("/dev/null", O_WRONLY);
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = key.generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 102U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, false, 104U));
    TEST_ASSERT_INT_EQ(1, (int)df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ((int)key.generation,
        (int)session->publication_generation);
    TEST_ASSERT_INT_EQ(1, session->reconnect_frame.data != NULL);
    trace.fail_control = fail_control;
    TEST_ASSERT_INT_EQ(fail_control ? DF_ERR_IO : DF_OK,
        df_media_session_manager_command(&manager,
            DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 200U));
    TEST_ASSERT_INT_EQ(0, (int)session->publication_generation);
    TEST_ASSERT_INT_EQ(1, session->reconnect_frame.data == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, entry.ready);
    TEST_ASSERT_INT_EQ(0, (int)status.active_encoders);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
        200U + DF_GVS_MONITOR_STOP_TIMEOUT_MS));
    TEST_ASSERT_INT_EQ(0, (int)df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(0, (int)session->generation);
    TEST_ASSERT_INT_EQ(1, (int)trace.resource_stops);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(0, (int)status.active_encoders);
    TEST_ASSERT_INT_EQ(0, (int)status.required_session_count);
    TEST_ASSERT_INT_EQ(0, (int)status.session_count);
    TEST_ASSERT_INT_EQ(1,
        df_media_session_manager_lookup(&manager, &key) == NULL);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_shared_viewer_stops_after_timeout(void) {
    manager_shared_viewer_stops_after_timeout(false);
    manager_shared_viewer_stops_after_timeout(true);
}

static int manager_start_pipeline_pair(struct df_media_session_manager *manager,
    struct df_media_module_config_v3 *config,
    struct df_media_module_callbacks_v3 *callbacks,
    struct manager_trace *trace, uint64_t *main_generation,
    uint64_t *side_generation) {
    manager_pipeline_fixture(config, callbacks, trace);
    if (df_media_session_manager_init(manager, config, callbacks) != DF_OK)
        return DF_ERR_IO;
    if (df_media_session_manager_start(manager, "gate_main",
            DF_MEDIA_SESSION_PREVIEW, 100U, main_generation) != DF_OK ||
        df_media_session_manager_start(manager, "gate_side",
            DF_MEDIA_SESSION_PREVIEW, 101U, side_generation) != DF_OK) {
        df_media_session_manager_destroy(manager);
        return DF_ERR_IO;
    }
    {
        static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
        struct df_gvs_frame frame = {
            .family = 0x03U,
            .opcode = 0x84U,
            .payload_length = sizeof(confirmation),
            .payload = confirmation,
        };

        memcpy(frame.destination, config->local, sizeof(frame.destination));
        memcpy(frame.source, config->stations[0].logical_address,
            sizeof(frame.source));
        if (df_media_session_manager_receive_control(manager, &frame,
                config->stations[0].ipv4, 102U) != DF_OK) {
            df_media_session_manager_destroy(manager);
            return DF_ERR_IO;
        }
        memcpy(frame.source, config->stations[1].logical_address,
            sizeof(frame.source));
        if (df_media_session_manager_receive_control(manager, &frame,
                config->stations[1].ipv4, 103U) != DF_OK) {
            df_media_session_manager_destroy(manager);
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}


void test_media_session_manager_routes_independent_encoder_pipelines(void) {
    static const uint8_t fragment_main[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x00U, 0x00U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x02U, 0x80U, 0x01U,
        0xe0U, 0x00U, 0xffU, 0xd9U,
    };
    static const uint8_t fragment_side[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x01U, 0x01U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x02U, 0x80U, 0x01U,
        0xe0U, 0x00U, 0xffU, 0xd9U,
    };
    static const uint8_t jpeg_main[] = {0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U};
    static const uint8_t jpeg_side[] = {0xffU, 0xd8U, 0x22U, 0xffU, 0xd9U};
    static const uint8_t jpeg_resized[] = {0xffU, 0xd8U, 0x33U, 0xffU, 0xd9U};
    const uint8_t main_station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t side_station[6] = {0x32U, 2U, 1U, 0U, 3U, 0U};
    const uint8_t unknown_station[6] = {0x32U, 2U, 1U, 0U, 2U, 9U};
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t wrong_destination[6] = {0x61U, 2U, 1U, 1U, 1U, 2U};
    const uint8_t expected_main[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x00U, 0x00U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x02U, 0x80U, 0x01U,
        0xe0U, 0x00U, 0xffU, 0xd9U,
        0xffU, 0xd8U, 0x11U, 0xffU, 0xd9U,
        0xffU, 0xd8U, 0x33U, 0xffU, 0xd9U,
    };
    const uint8_t expected_side[] = {
        0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x01U, 0x01U,
        0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x02U, 0x80U, 0x01U,
        0xe0U, 0x00U, 0xffU, 0xd9U,
        0xffU, 0xd8U, 0x22U, 0xffU, 0xd9U,
    };
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    char capture_directory[] = "/tmp/doorfast-media-pipelines-XXXXXX";
    char ffmpeg_program[256];
    char saved_path[4096];
    const char *path = getenv("PATH");
    uint8_t captured[32];
    uint64_t main_generation = 0U;
    uint64_t side_generation = 0U;
    struct df_media_session_status_v3 status_entries[2];
    struct df_media_module_status_v3 media_status = {
        .sessions = status_entries,
        .session_count = 2U,
    };
    uint64_t initial_status_revision;
    struct df_gvs_video_packet fragment = {
        .frame_no = 7U,
        .chunk_count = 2U,
        .chunk_length = 10U,
        .capacity = 10U,
        .full_length = 20U,
    };
    pid_t main_pid;
    pid_t side_pid;
    int length;

    TEST_ASSERT_INT_EQ(1, path != NULL && strlen(path) < sizeof(saved_path));
    if (path == NULL || strlen(path) >= sizeof(saved_path)) return;
    memcpy(saved_path, path, strlen(path) + 1U);
    TEST_ASSERT_INT_EQ(DF_OK, manager_write_ffmpeg_capture(capture_directory,
        ffmpeg_program, sizeof(ffmpeg_program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", capture_directory, 1));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_CAPTURE_DIR",
        capture_directory, 1));
    TEST_ASSERT_INT_EQ(DF_OK, manager_start_pipeline_pair(&manager, &config,
        &callbacks, &trace, &main_generation, &side_generation));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &media_status));
    TEST_ASSERT_INT_EQ(2, (int)media_status.session_count);
    TEST_ASSERT_INT_EQ(1, media_status.status_revision != 0U);
    TEST_ASSERT_INT_EQ(1, status_entries[0].status_revision != 0U);
    initial_status_revision = media_status.status_revision;

    memcpy(fragment.destination, local, sizeof(fragment.destination));
    memcpy(fragment.source, unknown_station, sizeof(fragment.source));
    fragment.chunk_index = 1U;
    fragment.payload = fragment_main;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_push_video(
            &manager, &fragment, 0x01020304U, 180U));
    memcpy(fragment.source, main_station, sizeof(fragment.source));
    memcpy(fragment.destination, wrong_destination,
        sizeof(fragment.destination));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_push_video(
            &manager, &fragment, 0x01020304U, 181U));
    memcpy(fragment.destination, local, sizeof(fragment.destination));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_push_video(
            &manager, &fragment, 0x01020399U, 182U));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(
        &manager, &fragment, 0x01020304U, 183U));
    memcpy(fragment.source, side_station, sizeof(fragment.source));
    fragment.payload = fragment_side;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(
        &manager, &fragment, 0x01020305U, 184U));
    TEST_ASSERT_INT_EQ(10, (int)manager.sessions[0].video.received);
    TEST_ASSERT_INT_EQ(10, (int)manager.sessions[1].video.received);
    memcpy(fragment.source, main_station, sizeof(fragment.source));
    fragment.chunk_index = 2U;
    fragment.payload = fragment_main + 10U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(
        &manager, &fragment, 0x01020304U, 185U));
    memcpy(fragment.source, side_station, sizeof(fragment.source));
    fragment.payload = fragment_side + 10U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(
        &manager, &fragment, 0x01020305U, 186U));
    media_status.session_count = 2U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_module_api_v3.status(&manager, &media_status));
    TEST_ASSERT_INT_EQ(1,
        media_status.status_revision != initial_status_revision);

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_push_jpeg(
        &manager, unknown_station, local, 0x01020304U, jpeg_main,
        sizeof(jpeg_main), 480U, 640U, 190U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_push_jpeg(
        &manager, main_station, wrong_destination, 0x01020304U, jpeg_main,
        sizeof(jpeg_main), 480U, 640U, 191U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_push_jpeg(
        &manager, main_station, local, 0x01020399U, jpeg_main,
        sizeof(jpeg_main), 480U, 640U, 192U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, main_station, local, 0x01020304U, jpeg_main,
        sizeof(jpeg_main), 480U, 640U, 200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, side_station, local, 0x01020305U, jpeg_side,
        sizeof(jpeg_side), 480U, 640U, 201U));
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[0].frames_received);
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[1].frames_received);
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[0].encoder.frames_written);
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[1].encoder.frames_written);
    main_pid = manager.sessions[0].encoder.pid;
    side_pid = manager.sessions[1].encoder.pid;
    TEST_ASSERT_INT_EQ(1, main_pid > 0 && side_pid > 0 && main_pid != side_pid);
    TEST_ASSERT_INT_EQ(
        (int)(sizeof(fragment_main) + sizeof(jpeg_main)),
        manager_wait_for_capture(capture_directory, "doorfast_gate_main",
            sizeof(fragment_main) + sizeof(jpeg_main)));
    TEST_ASSERT_INT_EQ(
        (int)(sizeof(fragment_side) + sizeof(jpeg_side)),
        manager_wait_for_capture(capture_directory, "doorfast_gate_side",
            sizeof(fragment_side) + sizeof(jpeg_side)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, main_station, local, 0x01020304U, jpeg_resized,
        sizeof(jpeg_resized), 360U, 480U, 202U));
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].encoder.pid != main_pid);
    TEST_ASSERT_INT_EQ((int)side_pid, (int)manager.sessions[1].encoder.pid);
    TEST_ASSERT_INT_EQ(3, (int)manager.sessions[0].frames_received);
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[1].frames_received);
    TEST_ASSERT_INT_EQ((int)sizeof(expected_main), manager_wait_for_capture(
        capture_directory, "doorfast_gate_main", sizeof(expected_main)));
    TEST_ASSERT_INT_EQ((int)sizeof(expected_side), manager_wait_for_capture(
        capture_directory, "doorfast_gate_side", sizeof(expected_side)));

    fragment.frame_no = 8U;
    fragment.chunk_index = 1U;
    memcpy(fragment.source, main_station, sizeof(fragment.source));
    fragment.payload = fragment_main;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_video(
        &manager, &fragment, 0x01020304U, 203U));
    TEST_ASSERT_INT_EQ(10, (int)manager.sessions[0].video.received);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
        &manager, "gate_main", 10U, 204U));
    TEST_ASSERT_INT_EQ(0, (int)manager.sessions[0].video.received);

    df_media_session_manager_destroy(&manager);
    length = manager_read_capture(capture_directory, "doorfast_gate_main",
        captured, sizeof(captured));
    TEST_ASSERT_INT_EQ((int)sizeof(expected_main), length);
    TEST_ASSERT_INT_EQ(0, length == (int)sizeof(expected_main) ?
        memcmp(captured, expected_main, sizeof(expected_main)) : -1);
    length = manager_read_capture(capture_directory, "doorfast_gate_side",
        captured, sizeof(captured));
    TEST_ASSERT_INT_EQ((int)sizeof(expected_side), length);
    TEST_ASSERT_INT_EQ(0, length == (int)sizeof(expected_side) ?
        memcmp(captured, expected_side, sizeof(expected_side)) : -1);

    TEST_ASSERT_INT_EQ(0, setenv("PATH", saved_path, 1));
    (void)unsetenv("DF_TEST_MEDIA_CAPTURE_DIR");
    {
        char main_capture[256];
        char side_capture[256];
        (void)snprintf(main_capture, sizeof(main_capture), "%s/%s",
            capture_directory, "doorfast_gate_main");
        (void)snprintf(side_capture, sizeof(side_capture), "%s/%s",
            capture_directory, "doorfast_gate_side");
        (void)unlink(main_capture);
        (void)unlink(side_capture);
        {
            char destinations[256];
            (void)snprintf(destinations, sizeof(destinations), "%s/%s",
                capture_directory, "destinations");
            (void)unlink(destinations);
        }
    }
    TEST_ASSERT_INT_EQ(0, unlink(ffmpeg_program));
    TEST_ASSERT_INT_EQ(0, rmdir(capture_directory));
}

void test_media_session_manager_isolates_encoder_exit(void) {
    static const uint8_t first[] = {0xffU, 0xd8U, 0x41U, 0xffU, 0xd9U};
    static const uint8_t second[] = {0xffU, 0xd8U, 0x42U, 0xffU, 0xd9U};
    const uint8_t main_station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t side_station[6] = {0x32U, 2U, 1U, 0U, 3U, 0U};
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_module_status_v3 status = {0};
    char capture_directory[] = "/tmp/doorfast-media-exit-pipelines-XXXXXX";
    char ffmpeg_program[256];
    char saved_path[4096];
    const char *path = getenv("PATH");
    uint64_t main_generation = 0U;
    uint64_t side_generation = 0U;
    uint64_t call_generation;
    pid_t side_pid;
    unsigned attempt;

    TEST_ASSERT_INT_EQ(1, path != NULL && strlen(path) < sizeof(saved_path));
    if (path == NULL || strlen(path) >= sizeof(saved_path)) return;
    memcpy(saved_path, path, strlen(path) + 1U);
    TEST_ASSERT_INT_EQ(DF_OK, manager_write_ffmpeg_capture(capture_directory,
        ffmpeg_program, sizeof(ffmpeg_program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", capture_directory, 1));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_CAPTURE_DIR",
        capture_directory, 1));
    TEST_ASSERT_INT_EQ(DF_OK, manager_start_pipeline_pair(&manager, &config,
        &callbacks, &trace, &main_generation, &side_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
        &manager, "gate_main", 10U, 150U));
    call_generation = manager.sessions[0].generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, main_station, local, 0x01020304U, first, sizeof(first),
        480U, 640U, 200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, side_station, local, 0x01020305U, first, sizeof(first),
        480U, 640U, 201U));
    side_pid = manager.sessions[1].encoder.pid;
    {
        struct timespec delay = {0, 100000000L};
        (void)nanosleep(&delay, NULL);
    }
    TEST_ASSERT_INT_EQ(0, kill(manager.sessions[0].encoder.pid, SIGKILL));
    for (attempt = 0U; attempt < 100U &&
            manager.failed_error == DF_MEDIA_ERROR_NONE; attempt++) {
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(
            &manager, 202U + attempt));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.status(&manager, &status));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED, status.failed_error);
    TEST_ASSERT_INT_EQ((int)call_generation, (int)status.failed_generation);
    {
        struct df_media_session_diagnostics_v2 current[2], last[2];
        struct df_media_module_diagnostics_v2 diagnostics = {
            .sessions = current, .session_count = 2U,
            .last_results = last, .last_result_count = 2U,
        };
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
            &manager, 400U, &diagnostics));
        TEST_ASSERT_INT_EQ(1, diagnostics.session_count);
        TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
        TEST_ASSERT_INT_EQ(0, strcmp("gate_main", last[0].station_id));
        TEST_ASSERT_INT_EQ(call_generation, last[0].generation);
        TEST_ASSERT_INT_EQ(DF_MEDIA_END_UNKNOWN, last[0].end_reason);
        TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_ENCODER_FAILED, last[0].failure_reason);
        TEST_ASSERT_INT_EQ(DF_MEDIA_TIMER_NONE, last[0].timer);
        TEST_ASSERT_INT_EQ(0, last[0].remaining_ms);
        TEST_ASSERT_INT_EQ(0, strcmp("gate_side", current[0].station_id));
        TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_NONE, current[0].failure_reason);
    }
    TEST_ASSERT_INT_EQ(0, (int)manager.sessions[0].generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PUBLISHING, manager.sessions[1].state);
    TEST_ASSERT_INT_EQ((int)side_pid, (int)manager.sessions[1].encoder.pid);
    TEST_ASSERT_INT_EQ(1,
        df_media_encoder_is_running(&manager.sessions[1].encoder) ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
        &manager, side_station, local, 0x01020305U, second, sizeof(second),
        480U, 640U, 400U));
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[1].frames_received);
    TEST_ASSERT_INT_EQ(2, (int)manager.sessions[1].encoder.frames_written);

    df_media_session_manager_destroy(&manager);
    TEST_ASSERT_INT_EQ(0, setenv("PATH", saved_path, 1));
    (void)unsetenv("DF_TEST_MEDIA_CAPTURE_DIR");
    {
        char main_capture[256];
        char side_capture[256];
        (void)snprintf(main_capture, sizeof(main_capture), "%s/%s",
            capture_directory, "doorfast_gate_main");
        (void)snprintf(side_capture, sizeof(side_capture), "%s/%s",
            capture_directory, "doorfast_gate_side");
        (void)unlink(main_capture);
        (void)unlink(side_capture);
        {
            char destinations[256];
            (void)snprintf(destinations, sizeof(destinations), "%s/%s",
                capture_directory, "destinations");
            (void)unlink(destinations);
        }
    }
    TEST_ASSERT_INT_EQ(0, unlink(ffmpeg_program));
    TEST_ASSERT_INT_EQ(0, rmdir(capture_directory));
}

void test_media_session_manager_recovers_preview_encoder_exit(void) {
    static const uint8_t first[] = {0xffU, 0xd8U, 0x41U, 0xffU, 0xd9U};
    static const uint8_t second[] = {0xffU, 0xd8U, 0x42U, 0xffU, 0xd9U};
    const uint8_t main_station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    char capture_directory[] = "/tmp/doorfast-media-preview-recovery-XXXXXX";
    char ffmpeg_program[256];
    char saved_path[4096];
    const char *path = getenv("PATH");
    uint64_t main_generation = 0U;
    struct df_media_session_key main_key;
    uint64_t media_generation;
    pid_t first_pid;
    unsigned attempt;

    TEST_ASSERT_INT_EQ(1, path != NULL && strlen(path) < sizeof(saved_path));
    if (path == NULL || strlen(path) >= sizeof(saved_path)) return;
    memcpy(saved_path, path, strlen(path) + 1U);
    TEST_ASSERT_INT_EQ(DF_OK, manager_write_ffmpeg_capture(capture_directory,
        ffmpeg_program, sizeof(ffmpeg_program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", capture_directory, 1));
    TEST_ASSERT_INT_EQ(0, setenv("DF_TEST_MEDIA_CAPTURE_DIR",
        capture_directory, 1));
    manager_pipeline_fixture(&config, &callbacks, &trace);
    config.station_count = 1U;
    config.max_encoders = 1U;
    if (df_media_session_manager_init(&manager, &config, &callbacks) != DF_OK)
        goto cleanup;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
        "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U, &main_generation));
    main_key.station_id = "gate_main";
    main_key.generation = main_generation;
    {
        static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
        struct df_gvs_frame frame = {
            .family = 0x03U,
            .opcode = 0x84U,
            .payload = confirmation,
            .payload_length = sizeof(confirmation),
        };
        memcpy(frame.destination, local, sizeof(frame.destination));
        memcpy(frame.source, main_station, sizeof(frame.source));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
            &manager, &frame, 0x01020304U, 101U));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &main_key, true, 102U));
    media_generation = manager.sessions[0].media_generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        main_station, local, 0x01020304U, first, sizeof(first), 480U, 640U,
        200U));
    first_pid = manager.sessions[0].encoder.pid;
    TEST_ASSERT_INT_EQ(1, first_pid > 0);
    TEST_ASSERT_INT_EQ(0, kill(first_pid, SIGKILL));
    for (attempt = 0U; attempt < 100U &&
            df_media_encoder_is_running(&manager.sessions[0].encoder); attempt++) {
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
            201U + attempt));
    }
    TEST_ASSERT_INT_EQ(1, (int)df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].active ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, manager.sessions[0].state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_NONE, manager.sessions[0].last_error);
    TEST_ASSERT_INT_EQ((int)main_generation,
        (int)manager.sessions[0].generation);
    TEST_ASSERT_INT_EQ((int)media_generation,
        (int)manager.sessions[0].media_generation);
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].viewer_active ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, manager.sessions[0].queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0,
        df_media_encoder_is_running(&manager.sessions[0].encoder) ? 1 : 0);
    manager_assert_current_diagnostic(&manager, 299U,
        DF_MEDIA_FAILURE_ENCODER_FAILED, DF_MEDIA_TIMER_NONE, 0U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        main_station, local, 0x01020304U, second, sizeof(second), 480U, 640U,
        300U));
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].encoder.pid > 0);
    TEST_ASSERT_INT_EQ(1, manager.sessions[0].encoder.pid != first_pid);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, manager.sessions[0].state);

    df_media_session_manager_destroy(&manager);
cleanup:
    TEST_ASSERT_INT_EQ(0, setenv("PATH", saved_path, 1));
    (void)unsetenv("DF_TEST_MEDIA_CAPTURE_DIR");
    {
        char capture[256];
        (void)snprintf(capture, sizeof(capture), "%s/%s", capture_directory,
            "doorfast_gate_main");
        (void)unlink(capture);
    }
    (void)unlink(ffmpeg_program);
    (void)rmdir(capture_directory);
}

static void outbound_fixture(struct df_media_session_manager *manager,
    struct manager_trace *trace, struct df_media_session_key *key) {
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}, .ipv4 = 0x01020304U},
        {.id = "gate_side", .stream_name = "doorfast_gate_side", .enabled = true,
         .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U}, .ipv4 = 0x01020305U},
    };
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct df_media_session_resource_hooks hooks = {
        .start = manager_start_resources, .stop = manager_stop_resources,
        .context = trace,
    };
    manager_fixture(&config, &callbacks, trace);
    config.stations = stations;
    config.station_count = 2U;
    config.local[0] = 0x61U;
    config.first_frame_timeout_s = 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_init(manager, &config, &callbacks));
    df_media_session_manager_set_resource_hooks(manager, &hooks);
    key->station_id = "gate_main";
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(manager,
        key->station_id, DF_MEDIA_SESSION_PREVIEW, 100U, &key->generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(manager, 100U));
}

static int outbound_confirm(struct df_media_session_manager *manager,
    uint64_t now_ms) {
    static const uint8_t payload[] = {0x1eU, 0U, 1U};
    struct df_media_session *session = &manager->sessions[0];
    struct df_gvs_frame frame = {.family = 3U, .opcode = 0x84U,
        .payload = payload, .payload_length = sizeof(payload)};
    memcpy(frame.source, session->station, 6U);
    memcpy(frame.destination, manager->config.local, 6U);
    return df_media_session_manager_receive_control(manager, &frame,
        session->station_ipv4, now_ms);
}

void test_media_session_manager_outbound_watchdogs_and_incoming_replacement(void) {
    for (unsigned scenario = 0U; scenario < 3U; scenario++) {
        struct manager_trace trace = {.available_kib = 4096U};
        struct df_media_session_manager manager = {0};
        struct df_media_session_key key;
        outbound_fixture(&manager, &trace, &key);
        TEST_ASSERT_INT_EQ(DF_OK, outbound_confirm(&manager, 101U));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
            "gate_main", DF_MEDIA_SESSION_CALL, 0U, 102U));
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 500U));
        TEST_ASSERT_INT_EQ(1, trace.control_count); /* only initial 03/04 */
        if (scenario == 1U) {
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
                DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 600U));
            unsigned controls = trace.control_count;
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 1600U));
            TEST_ASSERT_INT_EQ(controls, trace.control_count);
        } else {
            if (scenario == 2U) {
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_media_session_manager_outbound_operation(&manager,
                        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 44U, 500U));
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_media_session_manager_outbound_operation(&manager,
                        DF_MEDIA_OUTBOUND_MARK_SENT, &key, 44U, 500U));
                TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
                    "gate_main", DF_MEDIA_SESSION_CALL, 77U, 600U));
                TEST_ASSERT_INT_EQ(0, manager.sessions[0].outbound_handoff);
                TEST_ASSERT_INT_EQ(0, manager.sessions[0].outbound_command_sent);
                TEST_ASSERT_INT_EQ(0, manager.sessions[0].outbound_preview_persistent);
                TEST_ASSERT_INT_EQ(0, manager.sessions[0].preview_confirmation_current);
                TEST_ASSERT_INT_EQ(0, manager.sessions[0].preview_confirmation_generation);
                TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
                    df_media_session_manager_outbound_operation(&manager,
                        DF_MEDIA_OUTBOUND_RELEASE, &key, 44U, 601U));
                TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                    df_media_session_manager_outbound_operation(&manager,
                        DF_MEDIA_OUTBOUND_RELEASE,
                        &(struct df_media_session_key){"gate_main",
                            manager.sessions[0].generation}, 77U, 601U));
            }
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
                scenario == 2U ? 1600U : 1101U));
            TEST_ASSERT_INT_EQ(1, trace.control_count);
        }
        TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
        TEST_ASSERT_INT_EQ(1, trace.resource_stops);
        df_media_session_manager_destroy(&manager);
    }
}

void test_media_session_manager_outbound_bound_rollback_preserves_pipeline(void) {
    static const uint8_t jpeg[] = {0xffU, 0xd8U, 0xffU, 0xe0U, 0U, 4U, 0U, 0U,
        0xffU, 0xc0U, 0U, 8U, 8U, 1U, 0xe0U, 2U, 0x80U, 0U, 0xffU, 0xd9U};
    struct manager_trace trace = {.available_kib = 4096U};
    struct df_media_session_manager manager = {0};
    struct df_media_session_key key, stale;
    struct df_media_session *session;
    outbound_fixture(&manager, &trace, &key);
    TEST_ASSERT_INT_EQ(DF_OK, outbound_confirm(&manager, 101U));
    session = &manager.sessions[0];
    /* Only the external encoder process is replaced: real JPEG admission,
     * publication, frame queue, monitor, and viewer paths remain in use. */
    session->encoder.input_fd = open("/dev/null", O_WRONLY);
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = key.generation;
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(&manager,
        session->station, manager.config.local, session->station_ipv4,
        jpeg, sizeof(jpeg), 640U, 480U, 102U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&session->queue,
        jpeg, sizeof(jpeg), session->publication_generation, 103U));
    struct df_media_encoder_process encoder = session->encoder;
    struct df_media_frame_queue queue = session->queue;
    uint64_t publication = session->publication_generation;
    trace.available_kib = 0U;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RESOURCE_EXHAUSTED,
        df_media_module_api_v3.start(&manager, "gate_main",
            DF_MEDIA_SESSION_CALL, 0U, 104U));
    TEST_ASSERT_INT_EQ(0, memcmp(&encoder, &session->encoder, sizeof(encoder)));
    TEST_ASSERT_INT_EQ(0, memcmp(&queue, &session->queue, sizeof(queue)));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, session->purpose);
    TEST_ASSERT_INT_EQ(1, session->viewer_active);
    TEST_ASSERT_INT_EQ(0, trace.resource_stops);
    trace.available_kib = 4096U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
        "gate_main", DF_MEDIA_SESSION_CALL, 0U, 104U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
        "gate_main", DF_MEDIA_SESSION_CALL, 0U, 104U));
    TEST_ASSERT_INT_EQ(0, memcmp(&encoder, &session->encoder, sizeof(encoder)));
    TEST_ASSERT_INT_EQ(0, memcmp(&queue, &session->queue, sizeof(queue)));
    TEST_ASSERT_INT_EQ(publication, session->publication_generation);
    TEST_ASSERT_INT_EQ(key.generation, session->generation);
    TEST_ASSERT_INT_EQ(1, session->viewer_active);
    TEST_ASSERT_INT_EQ(1, trace.resource_starts);
    TEST_ASSERT_INT_EQ(0, trace.resource_stops);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 900U, 105U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 900U, 105U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_outbound_operation(&manager,
            DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 901U, 105U));
    stale = key; stale.generation++;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_outbound_operation(&manager,
            DF_MEDIA_OUTBOUND_ROLLBACK, &stale, 900U, 106U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_outbound_operation(&manager,
            DF_MEDIA_OUTBOUND_ROLLBACK, &key, 901U, 106U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_ROLLBACK, &key, 900U, 106U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, session->purpose);
    TEST_ASSERT_INT_EQ(0, session->call_generation);
    TEST_ASSERT_INT_EQ(0, memcmp(&encoder, &session->encoder, sizeof(encoder)));
    TEST_ASSERT_INT_EQ(0, memcmp(&queue, &session->queue, sizeof(queue)));
    TEST_ASSERT_INT_EQ(1, session->viewer_active);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
        "gate_main", DF_MEDIA_SESSION_CALL, 0U, 107U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 902U, 108U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_outbound_operation(&manager,
            DF_MEDIA_OUTBOUND_MARK_SENT, &key, 901U, 109U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_MARK_SENT, &key, 902U, 109U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 300U));
    TEST_ASSERT_INT_EQ(1, trace.control_count); /* live publisher, no 03/51 */
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_ROLLBACK, &key, 902U, 301U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_outbound_operation(&manager,
            DF_MEDIA_OUTBOUND_RELEASE, &key, 901U, 301U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_outbound_operation(&manager,
        DF_MEDIA_OUTBOUND_RELEASE, &key, 902U, 301U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
    TEST_ASSERT_INT_EQ(1, trace.resource_stops);
    TEST_ASSERT_INT_EQ(1, trace.control_count);
    df_media_session_manager_destroy(&manager);
}

void test_media_session_manager_outbound_negative_gates(void) {
    for (unsigned scenario = 0U; scenario < 13U; scenario++) {
        struct manager_trace trace = {.available_kib = 4096U};
        struct df_media_session_manager manager = {0};
        struct df_media_session_key key;
        static const uint8_t ack[] = {0x1eU, 0U, 1U};
        struct df_gvs_frame frame = {.family = 3U, .opcode = 0x84U,
            .payload = ack, .payload_length = sizeof(ack)};
        uint64_t check_time = 102U;
        int expected = DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED;
        outbound_fixture(&manager, &trace, &key);
        struct df_media_session *session = &manager.sessions[0];
        memcpy(frame.source, session->station, 6U);
        memcpy(frame.destination, manager.config.local, 6U);
        if (scenario < 4U) {
            if (scenario == 1U) frame.payload_length = 2U;
            if (scenario == 2U) frame.source[4]++;
            if (scenario != 0U) TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                df_media_session_manager_receive_control(&manager, &frame,
                    session->station_ipv4 + (scenario == 3U ? 1U : 0U), 101U));
        } else {
            TEST_ASSERT_INT_EQ(DF_OK, outbound_confirm(&manager,
                scenario == 4U ? 1101U : 101U));
            if (scenario == 4U) check_time = 1102U; /* expired request ACK */
            if (scenario == 5U) {
                check_time = 1101U; /* exact first-frame expiry boundary */
            }
            if (scenario == 6U) {
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_gvs_monitor_request_retry(&session->monitor, 102U));
            }
            if (scenario == 7U) {
                key.generation++;
                expected = DF_MEDIA_ERROR_GENERATION_MISMATCH;
            }
            if (scenario == 8U) {
                TEST_ASSERT_INT_EQ(DF_OK, df_media_module_api_v3.start(&manager,
                    "gate_side", DF_MEDIA_SESSION_CALL, 77U, 102U));
                expected = DF_MEDIA_ERROR_CAPACITY_BUSY;
            }
            if (scenario == 9U) {
                trace.available_kib = 0U;
                expected = DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;
            }
            if (scenario == 10U) {
                manager.stations[0].ipv4++;
                expected = DF_MEDIA_ERROR_ROUTE_UNAVAILABLE;
            }
            if (scenario == 11U) {
                key.station_id = "missing";
                expected = DF_MEDIA_ERROR_STATION_NOT_FOUND;
            }
            if (scenario == 12U) {
                frame.destination[0]++;
                TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                    df_media_session_manager_receive_control(&manager, &frame,
                        session->station_ipv4, 102U));
                /* Replace the accepted preview with its real next attempt,
                 * keeping historical diagnostics but closing current gate. */
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_gvs_monitor_request_retry(&session->monitor, 102U));
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_media_session_manager_tick(&manager, 102U));
                TEST_ASSERT_INT_EQ(DF_OK,
                    df_media_session_manager_tick(&manager, 1102U));
                check_time = 1103U;
            }
        }
        TEST_ASSERT_INT_EQ(expected, df_media_session_manager_outbound_operation(
            &manager, DF_MEDIA_OUTBOUND_CHECK, &key, 0U, check_time));
        TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, session->purpose);
        TEST_ASSERT_INT_EQ(1, session->generation);
        TEST_ASSERT_INT_EQ(0, trace.resource_stops);
        TEST_ASSERT_INT_EQ(scenario == 12U ? 2U : 1U, trace.control_count);
        df_media_session_manager_destroy(&manager);
    }
}
