#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "doorfast.h"
#include "media_session_manager.h"
#include "test.h"

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

    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->emit_control = manager_emit_control;
    callbacks->resolve_route = manager_resolve_route;
    callbacks->available_memory = manager_available_memory;
    callbacks->context = trace;
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

    memcpy(frame.source, stations[0].logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = 0x84U;
    frame.payload = confirmation;
    frame.payload_length = sizeof(confirmation);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 101U));

    frame.opcode = 0x51U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020301U, 102U));
    TEST_ASSERT_INT_EQ(1, (int)trace.control_count);
    frame.destination[5] ^= 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 102U));
    TEST_ASSERT_INT_EQ(1, (int)trace.control_count);
    frame.destination[5] ^= 1U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_manager_receive_control(&manager, &frame,
            0x01020300U, 102U));
    TEST_ASSERT_INT_EQ(2, (int)trace.control_count);
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
            0x01020300U, 103U));
    TEST_ASSERT_INT_EQ(2, (int)trace.control_count);

    frame.payload = NULL;
    frame.payload_length = 0U;
    trace.fail_control = true;
    {
        const struct df_gvs_monitor before = manager.sessions[0].monitor;

        TEST_ASSERT_INT_EQ(DF_ERR_IO,
            df_media_session_manager_receive_control(&manager, &frame,
                0x01020300U, 104U));
        TEST_ASSERT_INT_EQ(0, memcmp(&before, &manager.sessions[0].monitor,
            sizeof(before)));
    }
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
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
        &manager, "gate_main", generation + 1U, 501U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_manager_incoming_call(&manager, "gate_main",
            generation, 502U));
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
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(&manager,
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

void test_media_session_manager_correlates_stop_ack_and_releases_timeouts(void) {
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
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 1200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 2200U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 3200U));
    TEST_ASSERT_INT_EQ(0, df_media_session_manager_active(&manager));
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
    uint8_t contents[32];
    unsigned attempt;

    for (attempt = 0U; attempt < 1000U; attempt++) {
        int length = manager_read_capture(directory, stream, contents,
            sizeof(contents));
        struct timespec delay = {0, 1000000L};

        if (length >= 0 && (size_t)length >= expected_length) return length;
        (void)nanosleep(&delay, NULL);
    }
    return -1;
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
    char capture_directory[] = "/tmp/doorfast-media-exit-pipelines-XXXXXX";
    char ffmpeg_program[256];
    char saved_path[4096];
    const char *path = getenv("PATH");
    uint64_t main_generation = 0U;
    uint64_t side_generation = 0U;
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
            manager.sessions[0].state != DF_MEDIA_SESSION_FAILED; attempt++) {
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(
            &manager, 202U + attempt));
    }
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_FAILED, manager.sessions[0].state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED,
        manager.sessions[0].last_error);
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
