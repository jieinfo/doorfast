#include <stdio.h>
#include <string.h>

#include "media_module.h"
#include "runtime_media_module.h"
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
}

void test_runtime_module_rejects_incompatible_or_incomplete_api(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v3 config = {0};
    const struct df_media_module_callbacks_v3 callbacks = {0};
    struct df_media_module_api_v3 api = runtime_valid_api();

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
