#include <stdint.h>
#include <string.h>

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
};

static int manager_emit_control(const uint8_t destination[6],
    uint32_t destination_ipv4, const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, size_t payload_length,
    void *context) {
    (void)destination;
    (void)destination_ipv4;
    (void)source;
    (void)family;
    (void)opcode;
    (void)payload;
    (void)payload_length;
    (void)context;
    return DF_OK;
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
