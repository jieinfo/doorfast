#include "media_module.h"
#include "runtime_media_module.h"
#include "test.h"

static int runtime_fake_instance;
static unsigned runtime_fake_start_calls;
static uint64_t runtime_fake_start_now_ms;

static void runtime_fake_destroy(void *instance);
static int runtime_fake_control(void *instance,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms);
static int runtime_fake_tick(void *instance, uint64_t now_ms);

static void *runtime_fake_create_v3(
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    (void)config;
    (void)callbacks;
    return &runtime_fake_instance;
}

static int runtime_fake_start_v3(void *instance, const char *station_id,
    enum df_media_session_purpose purpose, uint64_t request_generation,
    uint64_t now_ms) {
    (void)instance; (void)station_id; (void)purpose;
    (void)request_generation; (void)now_ms; return DF_OK;
}

static int runtime_fake_command_v3(void *instance,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    (void)instance; (void)command; (void)key; (void)active;
    (void)now_ms; return DF_OK;
}

static int runtime_fake_jpeg_v3(void *instance, const uint8_t source[6],
    const uint8_t destination[6], uint32_t source_ipv4, const uint8_t *jpeg,
    size_t length, uint16_t width, uint16_t height, uint64_t timestamp_ms) {
    (void)instance; (void)source; (void)destination; (void)source_ipv4;
    (void)jpeg; (void)length; (void)width; (void)height;
    (void)timestamp_ms; return DF_OK;
}

static int runtime_fake_status_v3(const void *instance,
    struct df_media_module_status_v3 *status) {
    (void)instance; (void)status; return DF_OK;
}

static struct df_media_module_api_v3 runtime_valid_api_v3(void) {
    const struct df_media_module_api_v3 api = {
        .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
        .struct_size = sizeof(struct df_media_module_api_v3),
        .create = runtime_fake_create_v3,
        .destroy = runtime_fake_destroy,
        .start = runtime_fake_start_v3,
        .command = runtime_fake_command_v3,
        .receive_control = runtime_fake_control,
        .push_jpeg = runtime_fake_jpeg_v3,
        .tick = runtime_fake_tick,
        .status = runtime_fake_status_v3,
    };
    return api;
}

static void *runtime_fake_create(const struct df_media_module_config_v2 *config,
    const struct df_media_module_callbacks_v2 *callbacks) {
    (void)config;
    (void)callbacks;
    return &runtime_fake_instance;
}

static void runtime_fake_destroy(void *instance) { (void)instance; }
static int runtime_fake_start(void *instance, uint64_t now_ms) {
    (void)instance;
    runtime_fake_start_calls++;
    runtime_fake_start_now_ms = now_ms;
    return DF_OK;
}
static int runtime_fake_command(void *instance,
    enum df_media_module_command command, uint64_t generation, bool active,
    uint64_t now_ms) {
    (void)instance; (void)command; (void)generation; (void)active;
    (void)now_ms; return DF_OK;
}
static int runtime_fake_control(void *instance, const struct df_gvs_frame *frame,
    uint32_t source_ipv4, uint64_t now_ms) {
    (void)instance; (void)frame; (void)source_ipv4; (void)now_ms; return DF_OK;
}
static int runtime_fake_jpeg(void *instance, const uint8_t source[6],
    const uint8_t destination[6], uint32_t source_ipv4, uint64_t generation,
    const uint8_t *jpeg, size_t length, uint16_t width, uint16_t height,
    uint64_t timestamp_ms) {
    (void)instance; (void)source; (void)destination; (void)source_ipv4;
    (void)generation; (void)jpeg; (void)length; (void)width; (void)height;
    (void)timestamp_ms; return DF_OK;
}
static int runtime_fake_preempt(void *instance, uint64_t now_ms) {
    (void)instance; (void)now_ms; return DF_OK;
}
static int runtime_fake_tick(void *instance, uint64_t now_ms) {
    (void)instance; (void)now_ms; return DF_OK;
}
static int runtime_fake_status(const void *instance,
    struct df_media_module_status *status) {
    (void)instance; (void)status; return DF_OK;
}

static struct df_media_module_api_v2 runtime_valid_api(void) {
    const struct df_media_module_api_v2 api = {
        .abi_version = DF_MEDIA_MODULE_ABI_VERSION_V2,
        .struct_size = sizeof(struct df_media_module_api_v2),
        .create = runtime_fake_create,
        .destroy = runtime_fake_destroy,
        .start = runtime_fake_start,
        .command = runtime_fake_command,
        .receive_control = runtime_fake_control,
        .push_jpeg = runtime_fake_jpeg,
        .preempt = runtime_fake_preempt,
        .tick = runtime_fake_tick,
        .status = runtime_fake_status,
    };
    return api;
}

void test_runtime_module_loads_only_fixed_abi_and_fails_closed_when_missing(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v2 config = {0};
    const struct df_media_module_callbacks_v2 callbacks = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_media_module_start(&module,
        &config, &callbacks));
    TEST_ASSERT_INT_EQ(0, module.available);
}

void test_runtime_module_rejects_commands_when_unavailable(void) {
    struct df_runtime_media_module module = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_command(
        &module, DF_MEDIA_MODULE_COMMAND_STOP, 1, false, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_module_tick(
        &module, 10));
}

void test_runtime_module_rejects_incompatible_or_incomplete_api(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v2 config = {0};
    const struct df_media_module_callbacks_v2 callbacks = {0};
    struct df_media_module_api_v2 api = runtime_valid_api();

    api.abi_version++;
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

    {
        struct df_media_module_api_v3 api_v3 = runtime_valid_api_v3();
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_media_module_validate_api_v3(&api_v3));
        api_v3.abi_version = DF_MEDIA_MODULE_ABI_VERSION_V2;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
            df_runtime_media_module_validate_api_v3(&api_v3));
        api_v3 = runtime_valid_api_v3();
        api_v3.struct_size--;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
            df_runtime_media_module_validate_api_v3(&api_v3));
        api_v3 = runtime_valid_api_v3();
        api_v3.command = NULL;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
            df_runtime_media_module_validate_api_v3(&api_v3));
    }
}

void test_runtime_module_preserves_dynamic_library_handle(void) {
    struct df_runtime_media_module module = {0};
    const struct df_media_module_config_v2 config = {0};
    const struct df_media_module_callbacks_v2 callbacks = {0};
    const struct df_media_module_api_v2 api = runtime_valid_api();
    int handle_sentinel = 0;

    module.handle = &handle_sentinel;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(
        &module, &api, &config, &callbacks));
    TEST_ASSERT_INT_EQ(1, module.handle == &handle_sentinel);
    module.handle = NULL;
    df_runtime_media_module_stop(&module);
}

void test_runtime_module_request_start_calls_loaded_module(void) {
    const struct df_media_module_api_v2 api = runtime_valid_api();
    struct df_runtime_media_module module = {
        .api = &api,
        .instance = &runtime_fake_instance,
        .available = true,
    };

    runtime_fake_start_calls = 0U;
    runtime_fake_start_now_ms = 0U;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_module_request_start(&module, 42U));
    TEST_ASSERT_INT_EQ(1, (int)runtime_fake_start_calls);
    TEST_ASSERT_INT_EQ(42, (int)runtime_fake_start_now_ms);
    module.available = false;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_media_module_request_start(&module, 43U));
    TEST_ASSERT_INT_EQ(1, (int)runtime_fake_start_calls);
}
