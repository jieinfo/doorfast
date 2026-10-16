#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

#include "media_module.h"
#include "runtime_media_module.h"
#include "media_session_manager.h"
#include "test.h"

struct runtime_fake {
    unsigned start_calls;
    unsigned command_calls;
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    enum df_media_session_purpose purpose;
    uint64_t request_generation;
    uint64_t generation;
    uint64_t now_ms;
    struct df_media_session_key command_key;
    bool invalid_status_count;
};

static struct runtime_fake runtime_fake_instance;

struct malformed_diagnostic { unsigned mutation; bool terminal; };
static int runtime_malformed_diagnostic(const void *instance, uint64_t now_ms,
    struct df_media_module_diagnostics_v2 *status) {
    const struct malformed_diagnostic *malformed = instance;
    struct df_media_session_diagnostics_v2 *entry = malformed->terminal ?
        status->last_results : status->sessions;
    (void)now_ms;
    *entry = (struct df_media_session_diagnostics_v2){.station_id = "gate_main",
        .generation = 7, .timer = DF_MEDIA_TIMER_NONE,
        .end_reason = malformed->terminal ? DF_MEDIA_END_LOCAL_HANGUP : DF_MEDIA_END_NONE};
    switch (malformed->mutation) {
    case 0: entry->station_id[0] = '\0'; break;
    case 1: memset(entry->station_id, 'x', sizeof(entry->station_id)); break;
    case 2: entry->generation = 0; break;
    case 3: entry->timer = (enum df_media_timer)999; break;
    case 4: entry->end_reason = (enum df_media_end_reason)999; break;
    case 5: entry->failure_reason = (enum df_media_failure_reason)999; break;
    case 6: entry->remaining_ms = 1; break;
    case 7:
        if (malformed->terminal) entry->end_reason = DF_MEDIA_END_NONE;
        else entry->end_reason = DF_MEDIA_END_LOCAL_HANGUP;
        break;
    case 8: entry->timer = DF_MEDIA_TIMER_FIRST_FRAME; entry->remaining_ms = 10; break;
    }
    status->session_count = status->required_session_count = malformed->terminal ? 0 : 1;
    status->last_result_count = status->required_last_result_count = malformed->terminal ? 1 : 0;
    return DF_OK;
}

static int runtime_invalid_diagnostics_v2(const void *instance, uint64_t now_ms,
    struct df_media_module_diagnostics_v2 *status) {
    (void)instance; (void)now_ms;
    status->required_session_count = 2U;
    status->session_count = 2U;
    return DF_OK;
}

void test_runtime_media_module_diagnostics_optional_v2(void) {
    struct df_runtime_media_module module = {0};
    struct df_media_session_diagnostics_v2 current[1], last[1];
    struct df_media_module_diagnostics_v2 status = {
        .sessions = current, .session_count = 1U,
        .last_results = last, .last_result_count = 1U,
    };
    struct df_media_module_diagnostics_api_v2 invalid =
        df_media_module_diagnostics_api_v2;
    /* A working v3 module with no extension must report unavailable; the
     * bridge leaves enum fields unknown and omits remaining_ms. */
    module.api = &df_media_module_api_v3;
    module.instance = &runtime_fake_instance;
    module.available = true;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_diagnostics_v2(&module, 100U, &status));
    TEST_ASSERT_INT_EQ(0, status.session_count);
    TEST_ASSERT_INT_EQ(0, status.last_result_count);
    invalid.abi_version = 1U;
    module.diagnostics_api_v2 = &invalid;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_diagnostics_v2(&module, 100U, &status));
    invalid.abi_version = 2U;
    invalid.struct_size--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_diagnostics_v2(&module, 100U, &status));
    invalid.struct_size = sizeof(invalid);
    invalid.status = runtime_invalid_diagnostics_v2;
    status.session_count = 1U;
    status.last_result_count = 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
        df_runtime_media_module_diagnostics_v2(&module, 100U, &status));
    TEST_ASSERT_INT_EQ(0, status.session_count);
    TEST_ASSERT_INT_EQ(0, status.last_result_count);
    TEST_ASSERT_INT_EQ(0, strcmp("unknown",
        df_media_timer_name((enum df_media_timer)999)));
    TEST_ASSERT_INT_EQ(0, strcmp("unknown",
        df_media_end_reason_name((enum df_media_end_reason)999)));
    TEST_ASSERT_INT_EQ(0, strcmp("unknown",
        df_media_failure_reason_name((enum df_media_failure_reason)999)));
    invalid.status = runtime_malformed_diagnostic;
    for (unsigned terminal = 0; terminal < 2; terminal++) {
        for (unsigned mutation = 0; mutation < (terminal ? 9U : 8U); mutation++) {
            struct malformed_diagnostic malformed = {mutation, terminal != 0};
            module.instance = &malformed;
            status = (struct df_media_module_diagnostics_v2){.sessions = current,
                .session_count = 1, .last_results = last, .last_result_count = 1};
            TEST_ASSERT_INT_EQ(DF_ERR_IO,
                df_runtime_media_module_diagnostics_v2(&module, 100, &status));
            TEST_ASSERT_INT_EQ(1, status.sessions == current);
            TEST_ASSERT_INT_EQ(1, status.last_results == last);
            TEST_ASSERT_INT_EQ(0, status.session_count);
            TEST_ASSERT_INT_EQ(0, status.last_result_count);
            TEST_ASSERT_INT_EQ(0, status.required_session_count);
            TEST_ASSERT_INT_EQ(0, status.required_last_result_count);
        }
    }
}

static void *runtime_fake_create(
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    (void)config;
    (void)callbacks;
    return &runtime_fake_instance;
}

static void runtime_fake_destroy(void *instance) { (void)instance; }

static int runtime_fake_start(void *instance, const char *station_id,
    enum df_media_session_purpose purpose, uint64_t request_generation,
    uint64_t now_ms) {
    struct runtime_fake *fake = instance;

    fake->start_calls++;
    (void)snprintf(fake->station_id, sizeof(fake->station_id), "%s",
        station_id);
    fake->purpose = purpose;
    fake->request_generation = request_generation;
    fake->generation = request_generation == 0U ? 21U : request_generation;
    fake->now_ms = now_ms;
    return DF_OK;
}

static int runtime_fake_command(void *instance,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    struct runtime_fake *fake = instance;

    (void)command;
    (void)active;
    fake->command_calls++;
    fake->command_key = *key;
    fake->now_ms = now_ms;
    return DF_OK;
}

static int runtime_fake_control(void *instance,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    (void)instance;
    (void)frame;
    (void)source_ipv4;
    (void)now_ms;
    return DF_OK;
}

static int runtime_fake_jpeg(void *instance, const uint8_t source[6],
    const uint8_t destination[6], uint32_t source_ipv4, const uint8_t *jpeg,
    size_t length, uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    (void)instance;
    (void)source;
    (void)destination;
    (void)source_ipv4;
    (void)jpeg;
    (void)length;
    (void)width;
    (void)height;
    (void)timestamp_ms;
    return DF_OK;
}

static int runtime_fake_video(void *instance,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4,
    uint64_t timestamp_ms) {
    (void)instance;
    (void)packet;
    (void)source_ipv4;
    (void)timestamp_ms;
    return DF_OK;
}

static int runtime_fake_tick(void *instance, uint64_t now_ms) {
    (void)instance;
    (void)now_ms;
    return DF_OK;
}

static int runtime_fake_status(const void *instance,
    struct df_media_module_status_v3 *status) {
    const struct runtime_fake *fake = instance;

    status->required_session_count = fake->generation == 0U ? 0U : 1U;
    status->configured_capacity = 2U;
    status->effective_capacity = 2U;
    status->active_encoders = fake->generation == 0U ? 0U : 1U;
    if (status->sessions != NULL && status->session_count > 0U &&
        fake->generation != 0U) {
        (void)snprintf(status->sessions[0].station_id,
            sizeof(status->sessions[0].station_id), "%s", fake->station_id);
        status->sessions[0].generation = fake->generation;
        status->sessions[0].purpose = fake->purpose;
        status->sessions[0].active = true;
        status->session_count = 1U;
    } else {
        status->session_count = 0U;
    }
    if (fake->invalid_status_count) {
        status->required_session_count = 3U;
        status->session_count = 3U;
    }
    return DF_OK;
}

static struct df_media_module_api_v3 runtime_valid_api(void) {
    const struct df_media_module_api_v3 api = {
        .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
        .struct_size = sizeof(struct df_media_module_api_v3),
        .create = runtime_fake_create,
        .destroy = runtime_fake_destroy,
        .start = runtime_fake_start,
        .command = runtime_fake_command,
        .receive_control = runtime_fake_control,
        .push_jpeg = runtime_fake_jpeg,
        .push_video = runtime_fake_video,
        .tick = runtime_fake_tick,
        .status = runtime_fake_status,
    };
    return api;
}

void test_runtime_module_loads_only_fixed_abi_and_fails_closed_when_missing(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v3 config = {0};
    const struct df_media_module_callbacks_v3 callbacks = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_media_module_start(&module,
        &config, &callbacks));
    TEST_ASSERT_INT_EQ(0, module.available);
}

void test_runtime_module_rejects_commands_when_unavailable(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_session_key key = {
        .station_id = "gate_main", .generation = 1U,
    };

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_command(
        &module, DF_MEDIA_MODULE_COMMAND_STOP, &key, false, 10U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_tick(
        &module, 10U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_unlock_prepare_start(&module,
            "gate_main", 10U, &(uint64_t){0U}));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_unlock_prepare_status(&module, "gate_main",
            1U, &(struct df_media_unlock_prepare_status_v1){0}));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_unlock_prepare_stop(&module,
            &key, 10U));
}

void test_runtime_module_rejects_incompatible_or_incomplete_api(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v3 config = {0};
    const struct df_media_module_callbacks_v3 callbacks = {0};
    struct df_media_module_api_v3 api = runtime_valid_api();
    struct df_media_module_unlock_prepare_api_v1 prepare_api =
        df_media_module_unlock_prepare_api_v1;

    api.abi_version = DF_MEDIA_MODULE_ABI_VERSION_V2;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    api = runtime_valid_api();
    api.struct_size--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    api = runtime_valid_api();
    api.tick = NULL;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    api = runtime_valid_api();
    api.push_video = NULL;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));

    /* A valid legacy v3 module remains usable without the optional symbol. */
    api = runtime_valid_api();
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_unlock_prepare_start(&module,
            "gate_main", 10U, &(uint64_t){0U}));
    prepare_api.abi_version++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_unlock_prepare_api(&module, &prepare_api));
    TEST_ASSERT_INT_EQ(1, module.unlock_prepare_invalid);
    prepare_api = df_media_module_unlock_prepare_api_v1;
    prepare_api.struct_size--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_unlock_prepare_api(&module, &prepare_api));
    prepare_api = df_media_module_unlock_prepare_api_v1;
    prepare_api.status = NULL;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_unlock_prepare_api(&module, &prepare_api));
    prepare_api = df_media_module_unlock_prepare_api_v1;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_module_set_unlock_prepare_api(&module, &prepare_api));
    TEST_ASSERT_INT_EQ(0, module.unlock_prepare_invalid);
    df_runtime_media_module_stop(&module);
}

void test_runtime_module_preserves_dynamic_library_handle(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v3 config = {0};
    const struct df_media_module_callbacks_v3 callbacks = {0};
    const struct df_media_module_api_v3 api = runtime_valid_api();
    int handle_sentinel = 0;

    module.handle = &handle_sentinel;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    TEST_ASSERT_INT_EQ(1, module.handle == &handle_sentinel);
    module.handle = NULL;
    df_runtime_media_module_stop(&module);
}

void test_runtime_module_request_start_calls_loaded_module(void) {
    const struct df_media_module_api_v3 api = runtime_valid_api();
    struct df_media_session_status_v3 snapshot[2];
    struct df_runtime_media_module module = {
        .api = &api,
        .instance = &runtime_fake_instance,
        .session_snapshot = snapshot,
        .session_snapshot_capacity = 2U,
        .available = true,
    };
    struct df_media_session_key key = {
        .station_id = "gate_main", .generation = 21U,
    };
    uint64_t generation = 0U;

    memset(&runtime_fake_instance, 0, sizeof(runtime_fake_instance));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_request_start(
        &module, "gate_main", 42U, &generation));
    TEST_ASSERT_INT_EQ(1, (int)runtime_fake_instance.start_calls);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", runtime_fake_instance.station_id));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW,
        runtime_fake_instance.purpose);
    TEST_ASSERT_INT_EQ(0, (int)runtime_fake_instance.request_generation);
    TEST_ASSERT_INT_EQ(21, (int)generation);
    runtime_fake_instance.invalid_status_count = true;
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_media_module_request_start(
        &module, "gate_main", 42U, &generation));
    runtime_fake_instance.invalid_status_count = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_VIEWER, &key, true, 43U));
    TEST_ASSERT_INT_EQ(1, (int)runtime_fake_instance.command_calls);
    TEST_ASSERT_INT_EQ(1, runtime_fake_instance.command_key.station_id ==
        key.station_id);
    TEST_ASSERT_INT_EQ(21,
        (int)runtime_fake_instance.command_key.generation);
    module.available = false;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_request_start(
        &module, "gate_main", 44U, &generation));
}

void test_runtime_media_outbound_extension_fails_closed(void) {
    const struct df_media_module_api_v3 api = runtime_valid_api();
    struct df_media_session_status_v3 snapshot[1];
    struct df_runtime_media_module module = {.api = &api,
        .instance = &runtime_fake_instance, .available = true,
        .session_snapshot = snapshot, .session_snapshot_capacity = 1U};
    struct df_media_session_key key = {"gate_main", 21U};
    struct df_media_module_outbound_upgrade_api_v1 extension =
        df_media_module_outbound_upgrade_api_v1;
    uint64_t generation = 99U;
    memset(&runtime_fake_instance, 0, sizeof(runtime_fake_instance));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_call(&module, "gate_main", 100U, &generation));
    TEST_ASSERT_INT_EQ(0, generation);
    TEST_ASSERT_INT_EQ(0, runtime_fake_instance.start_calls);
    /* The dynamically loaded old-module case must also never call old CALL. */
    int handle = 0;
    module.handle = &handle;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_call(&module, "gate_main", 100U, &generation));
    TEST_ASSERT_INT_EQ(0, runtime_fake_instance.start_calls);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_call_key(&module, &key, 100U));
    extension.abi_version++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_outbound_api(&module, &extension));
    extension = df_media_module_outbound_upgrade_api_v1;
    extension.struct_size--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_outbound_api(&module, &extension));
    extension = df_media_module_outbound_upgrade_api_v1;
    extension.operation = NULL;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_set_outbound_api(&module, &extension));
    TEST_ASSERT_INT_EQ(1, module.outbound_upgrade_api == NULL);
    struct df_media_module_lifecycle_api_v2 lifecycle = df_media_module_lifecycle_api_v2;
    lifecycle.abi_version = 1;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_set_lifecycle_api(&module, &lifecycle));
    lifecycle = df_media_module_lifecycle_api_v2;
    lifecycle.struct_size--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_set_lifecycle_api(&module, &lifecycle));
    lifecycle = df_media_module_lifecycle_api_v2;
    lifecycle.operation = NULL;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_set_lifecycle_api(&module, &lifecycle));
    TEST_ASSERT_INT_EQ(1, module.lifecycle_api == NULL);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_module_outbound_upgrade_api_v1.operation(
        NULL, DF_MEDIA_INCOMING_RELEASE_LOCAL, &key, 41, 100));

    /* Exercise the same dlopen API/instance boundary as runtime startup with
     * an actual legacy v3 library, rather than inferring support from v3. */
    struct df_runtime_media_module legacy = {0};
    const struct df_media_module_api_v3 *legacy_api = NULL;
    unsigned *legacy_calls = NULL;
    legacy.handle = dlopen("build/media-v3-legacy.so", RTLD_NOW | RTLD_LOCAL);
    TEST_ASSERT_INT_EQ(1, legacy.handle != NULL);
    if (legacy.handle == NULL) return;
    *(void **)(&legacy_api) = dlsym(legacy.handle, "df_media_module_api_v3");
    *(void **)(&legacy_calls) = dlsym(legacy.handle, "df_test_legacy_call_count");
    TEST_ASSERT_INT_EQ(1, legacy_api != NULL && legacy_calls != NULL);
    if (legacy_api == NULL || legacy_calls == NULL) {
        (void)dlclose(legacy.handle);
        return;
    }
    struct df_media_module_config_v3 config = {.station_count = 1U};
    struct df_media_module_callbacks_v3 callbacks = {0};
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(&legacy,
        legacy_api, &config, &callbacks));
    TEST_ASSERT_INT_EQ(1,
        dlsym(legacy.handle, "df_media_module_outbound_upgrade_api_v1") == NULL);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_call(&legacy,
        "gate_main", 100U, &generation));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_call_key(&legacy, &key, 100U));
    TEST_ASSERT_INT_EQ(0, *legacy_calls);
    df_runtime_media_module_stop(&legacy);
}

static int outbound_runtime_emit(const uint8_t destination[6], uint32_t ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t length, void *context) {
    (void)destination; (void)ipv4; (void)source; (void)payload; (void)length;
    TEST_ASSERT_INT_EQ(3, family);
    TEST_ASSERT_INT_EQ(4, opcode);
    (*(unsigned *)context)++;
    return DF_OK;
}

static int outbound_runtime_route(const uint8_t peer[6], uint64_t now_ms,
    uint32_t *ipv4, void *context) {
    (void)peer; (void)now_ms; (void)context;
    *ipv4 = 0x01020304U;
    return DF_OK;
}

static int outbound_runtime_memory(uint64_t *available, void *context) {
    (void)context;
    *available = 4096U;
    return DF_OK;
}

void test_runtime_media_outbound_uses_real_manager(void) {
    static const struct df_media_station_config_v3 station = {
        .id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
        .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U}, .ipv4 = 0x01020304U};
    const struct df_media_module_config_v3 config = {.enabled = true,
        .local = {0x61U, 2U, 1U, 1U, 1U, 1U}, .stations = &station,
        .station_count = 1U, .max_encoders = 1U, .fps = 10U};
    unsigned sends = 0U;
    const struct df_media_module_callbacks_v3 callbacks = {
        .emit_control = outbound_runtime_emit, .resolve_route = outbound_runtime_route,
        .available_memory = outbound_runtime_memory,
        .context = &sends};
    struct df_runtime_media_module module = {0};
    struct df_media_session_key key = {.station_id = "gate_main"};
    uint64_t generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(&module,
        &df_media_module_api_v3, &config, &callbacks));
    TEST_ASSERT_INT_EQ(1, module.outbound_upgrade_api == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_set_outbound_api(&module,
        &df_media_module_outbound_upgrade_api_v1));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_request_start(&module,
        "gate_main", 100U, &key.generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&module, 100U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_PREVIEW_UNCONFIRMED,
        df_runtime_media_module_call(&module, "gate_main", 101U, &generation));
    static const uint8_t ack[] = {0x1eU, 0U, 1U};
    struct df_gvs_frame frame = {.family = 3U, .opcode = 0x84U,
        .payload = ack, .payload_length = sizeof(ack)};
    memcpy(frame.source, station.logical_address, 6U);
    memcpy(frame.destination, config.local, 6U);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_receive_control(&module,
        &frame, station.ipv4, 102U));
    struct df_media_session_key stale = key; stale.generation++;
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_runtime_media_module_call_key(&module, &stale, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_call_key(&module, &key, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_call(&module,
        "gate_main", 104U, &generation));
    TEST_ASSERT_INT_EQ(key.generation, generation);
    TEST_ASSERT_INT_EQ(1, sends);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_outbound_operation(&module,
        DF_MEDIA_OUTBOUND_BIND_CONTROL, &key, 44U, 105U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_outbound_operation(&module,
        DF_MEDIA_OUTBOUND_ROLLBACK, &key, 44U, 106U));
    const struct df_media_session *session =
        df_media_session_manager_find(module.instance, "gate_main");
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, session->purpose);
    TEST_ASSERT_INT_EQ(key.generation, session->generation);
    df_runtime_media_module_stop(&module);
}
