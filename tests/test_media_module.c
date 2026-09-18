#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "gvs_frame.h"
#include "media_module.h"
#include "test.h"

typedef int (*expected_deprecated_relay_send_fn)(const char *, const char *,
    const char *, void *);

_Static_assert(DF_MEDIA_MODULE_ABI_VERSION == 2U,
    "Task 6 must not renumber the media module ABI");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(struct df_media_module_config_v2) == 96U,
    "media ABI v2 config size changed");
_Static_assert(offsetof(struct df_media_module_config_v2,
    deprecated_relay_url) == 88U, "media ABI v2 config relay slot moved");
_Static_assert(sizeof(struct df_media_module_callbacks_v2) == 40U,
    "media ABI v2 callbacks size changed");
_Static_assert(offsetof(struct df_media_module_callbacks_v2,
    deprecated_relay_send) == 16U, "media ABI v2 callback relay slot moved");
_Static_assert(sizeof(struct df_media_module_status) == 152U,
    "media ABI v2 status size changed");
_Static_assert(offsetof(struct df_media_module_status,
    deprecated_relay_failures) == 148U, "media ABI v2 status relay slot moved");
_Static_assert(sizeof(struct df_media_module_api_v2) == 80U,
    "media ABI v2 API size changed");
#endif
_Static_assert(_Generic(((struct df_media_module_config_v2 *)0)->
    deprecated_relay_url, const char *: 1, default: 0),
    "media ABI v2 config relay slot type changed");
_Static_assert(_Generic(((struct df_media_module_callbacks_v2 *)0)->
    deprecated_relay_send, expected_deprecated_relay_send_fn: 1, default: 0),
    "media ABI v2 callback relay slot type changed");
_Static_assert(_Generic(((struct df_media_module_status *)0)->
    deprecated_relay_failures, unsigned: 1, default: 0),
    "media ABI v2 status relay slot type changed");

struct module_trace {
    char entries[8][32];
    unsigned count;
    unsigned deprecated_relay_calls;
    uint64_t available_memory_kib;
};

static int module_available_memory(uint64_t *available_kib, void *context) {
    struct module_trace *trace = context;

    if (available_kib == NULL || trace == NULL) return DF_ERR_INVALID;
    *available_kib = trace->available_memory_kib;
    return DF_OK;
}

static int module_write_credentials(char path[]) {
    static const char contents[] = "rtsp_password=rtsp-secret\n";
    int descriptor = mkstemp(path);

    if (descriptor < 0 || fchmod(descriptor, 0600) != 0 ||
        write(descriptor, contents, sizeof(contents) - 1U) !=
            (ssize_t)(sizeof(contents) - 1U) || close(descriptor) != 0) {
        if (descriptor >= 0) (void)close(descriptor);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int module_emit_control(const uint8_t destination[6],
    uint32_t destination_ipv4, const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, size_t payload_length,
    void *context) {
    struct module_trace *trace = context;
    (void)destination;
    (void)destination_ipv4;
    (void)source;
    (void)family;
    (void)opcode;
    (void)payload;
    (void)payload_length;
    (void)snprintf(trace->entries[trace->count++],
        sizeof(trace->entries[0]), "control");
    return DF_OK;
}

static int module_noop_control(const uint8_t destination[6],
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

static int module_write_program_stub(char directory[], char program[],
    size_t program_capacity, const char *name) {
    static const char script[] = "#!/bin/sh\nIFS= read -r ignored || exit 0\n";
    int descriptor;

    if (mkdtemp(directory) == NULL ||
        name == NULL || snprintf(program, program_capacity, "%s/%s", directory,
            name) >= (int)program_capacity)
        return DF_ERR_IO;
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

static int module_write_encoder_stub(char directory[], char program[],
    size_t program_capacity) {
    return module_write_program_stub(directory, program, program_capacity,
        "encoder");
}

static int module_write_ffmpeg_stub(char directory[], char program[],
    size_t program_capacity) {
    return module_write_program_stub(directory, program, program_capacity,
        "ffmpeg");
}

static int module_deprecated_relay_send(const char *url, const char *token,
    const char *json, void *context) {
    struct module_trace *trace = context;
    (void)url;
    (void)token;
    (void)json;
    trace->deprecated_relay_calls++;
    return DF_OK;
}

static int module_fail_encoder_stop(struct df_media_encoder_process *encoder,
    unsigned timeout_ms) {
    (void)encoder;
    (void)timeout_ms;
    return DF_ERR_IO;
}

static void module_set_running_media(struct df_media_module *module,
    uint64_t generation, uint64_t now_ms) {
    module->monitor.state = DF_GVS_MONITOR_PUBLISHING;
    module->monitor.generation = generation;
    module->monitor.last_now_ms = now_ms;
    module->monitor.station_ipv4 = 0x01020304U;
    module->monitor.media_ready = true;
    memcpy(module->monitor.local,
        (const uint8_t[]){0x61, 2, 1, 1, 1, 1}, 6U);
    memcpy(module->monitor.station,
        (const uint8_t[]){0x32, 2, 1, 0, 2, 0}, 6U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(
        &module->queue, generation, DF_GVS_VIDEO_MAX_FRAME));
    module->queue_initialized = true;
    module->encoder.running = true;
    module->encoder.generation = generation;
    module->encoder.input_fd = -1;
}

void test_media_module_applies_resource_and_timeout_config(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {.available_memory_kib = 131071U};
    char path[] = "/tmp/doorfast-media-limits-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = htonl(INADDR_LOOPBACK),
        .go2rtc_host = "127.0.0.1",
        .go2rtc_port = 8554U,
        .stream_name = "doorfast_preview",
        .rtsp_username = "doorfast",
        .credentials_path = path,
        .encoder = DF_MEDIA_ENCODER_SOFTWARE,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .fps = 10U,
        .bitrate_kbps = 800U,
        .profile = DF_MEDIA_PROFILE_BASELINE,
        .min_free_kib = 131072U,
        .preview_timeout_s = 15U,
        .first_frame_timeout_s = 2U,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_emit_control,
        .available_memory = module_available_memory,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(
        &module, &config, &callbacks, 100U));
    TEST_ASSERT_INT_EQ(2000, (int)module.monitor.first_frame_timeout_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_module_start(&module, 100U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, module.monitor.state);
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, strcmp("insufficient_memory", module.failure));

    trace.available_memory_kib = 131072U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 100U));
    TEST_ASSERT_INT_EQ(1, (int)trace.count);
    module.monitor.state = DF_GVS_MONITOR_PUBLISHING;
    module.monitor.media_ready = true;
    module.monitor.last_now_ms = 100U;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 15099U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_PUBLISHING, module.monitor.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 15100U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_STOPPING, module.monitor.state);
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_preempts_without_deprecated_relay_callback(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-preempt-XXXXXX";
    struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_emit_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 100));
    TEST_ASSERT_INT_EQ(1, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, strcmp("control", trace.entries[0]));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_preempt(&module, 110));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, strcmp("control", trace.entries[1]));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 110));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_restarts_encoder_when_source_dimensions_change(void) {
    const uint8_t jpeg[] = {0xff, 0xd8, 0x01, 0xff, 0xd9};
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char credentials_path[] = "/tmp/doorfast-media-dimensions-XXXXXX";
    char encoder_directory[] = "/tmp/doorfast-media-encoder-XXXXXX";
    char encoder_program[128];
    char original_path[4096];
    const char *path_value = getenv("PATH");
    pid_t first_pid;
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = htonl(INADDR_LOOPBACK),
        .go2rtc_host = "127.0.0.1",
        .go2rtc_port = 8554,
        .stream_name = "doorfast_preview",
        .rtsp_username = "doorfast",
        .credentials_path = credentials_path,
        .encoder = DF_MEDIA_ENCODER_SOFTWARE,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .fps = 10,
        .bitrate_kbps = 800,
        .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_emit_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(1, path_value != NULL &&
        strlen(path_value) < sizeof(original_path));
    if (path_value != NULL && strlen(path_value) < sizeof(original_path))
        memcpy(original_path, path_value, strlen(path_value) + 1U);
    else
        original_path[0] = '\0';
    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(credentials_path));
    TEST_ASSERT_INT_EQ(DF_OK, module_write_encoder_stub(encoder_directory,
        encoder_program, sizeof(encoder_program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", encoder_directory, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 100U));
    module.monitor.state = DF_GVS_MONITOR_PUBLISHING;
    module.monitor.generation = 1U;
    module.monitor.last_now_ms = 100U;
    module.monitor.media_ready = true;
    module.monitor.station_ipv4 = htonl(INADDR_LOOPBACK);
    memcpy(module.monitor.local, local, sizeof(local));
    memcpy(module.monitor.station, station, sizeof(station));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_push_jpeg(&module, station,
        local, htonl(INADDR_LOOPBACK), 1U, jpeg, sizeof(jpeg), 480U, 640U,
        100U));
    TEST_ASSERT_INT_EQ(1, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(480, module.encoder.source_width);
    TEST_ASSERT_INT_EQ(640, module.encoder.source_height);
    first_pid = module.encoder.pid;
    TEST_ASSERT_INT_EQ(1, first_pid > 0 ? 1 : 0);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_push_jpeg(&module, station,
        local, htonl(INADDR_LOOPBACK), 1U, jpeg, sizeof(jpeg), 360U, 480U,
        101U));
    TEST_ASSERT_INT_EQ(1, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(360, module.encoder.source_width);
    TEST_ASSERT_INT_EQ(480, module.encoder.source_height);
    TEST_ASSERT_INT_EQ(1, module.encoder.pid != first_pid ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, setenv("PATH", original_path, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(credentials_path));
    TEST_ASSERT_INT_EQ(0, unlink(encoder_program));
    TEST_ASSERT_INT_EQ(0, rmdir(encoder_directory));
}

void test_media_module_encoder_exit_fails_generation_and_cleans_media(void) {
    const uint8_t jpeg[] = {0xff, 0xd8, 0x01, 0xff, 0xd9};
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char credentials_path[] = "/tmp/doorfast-media-exit-XXXXXX";
    char encoder_directory[] = "/tmp/doorfast-media-exit-encoder-XXXXXX";
    char encoder_program[128];
    char original_path[4096];
    const char *path_value = getenv("PATH");
    unsigned attempt;
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = htonl(INADDR_LOOPBACK),
        .go2rtc_host = "127.0.0.1",
        .go2rtc_port = 8554,
        .stream_name = "doorfast_preview",
        .rtsp_username = "doorfast",
        .credentials_path = credentials_path,
        .encoder = DF_MEDIA_ENCODER_SOFTWARE,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .fps = 10,
        .bitrate_kbps = 800,
        .profile = DF_MEDIA_PROFILE_BASELINE,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(1, path_value != NULL &&
        strlen(path_value) < sizeof(original_path));
    if (path_value != NULL && strlen(path_value) < sizeof(original_path))
        memcpy(original_path, path_value, strlen(path_value) + 1U);
    else
        original_path[0] = '\0';
    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(credentials_path));
    TEST_ASSERT_INT_EQ(DF_OK, module_write_ffmpeg_stub(encoder_directory,
        encoder_program, sizeof(encoder_program)));
    TEST_ASSERT_INT_EQ(0, setenv("PATH", encoder_directory, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 100U));
    module.monitor.state = DF_GVS_MONITOR_PUBLISHING;
    module.monitor.generation = 1U;
    module.monitor.last_now_ms = 100U;
    module.monitor.media_ready = true;
    module.monitor.station_ipv4 = htonl(INADDR_LOOPBACK);
    memcpy(module.monitor.local, local, sizeof(local));
    memcpy(module.monitor.station, station, sizeof(station));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_push_jpeg(&module, station,
        local, htonl(INADDR_LOOPBACK), 1U, jpeg, sizeof(jpeg), 480U, 640U,
        100U));
    TEST_ASSERT_INT_EQ(1, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, kill(module.encoder.pid, SIGKILL));
    for (attempt = 0U; attempt < 100U && !module.encoder.encoder_exited;
         ++attempt) {
        struct timespec delay = {0, 1000000L};

        (void)nanosleep(&delay, NULL);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(
            &module, 101U + attempt));
    }

    TEST_ASSERT_INT_EQ(1, module.encoder.encoder_exited ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, module.monitor.state);
    TEST_ASSERT_INT_EQ(0, strcmp("encoder_exited", module.failure));
    TEST_ASSERT_INT_EQ(0, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, (int)module.encoder.pid);
    TEST_ASSERT_INT_EQ(0, module.queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(1, (int)module.monitor.generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 202U));
    TEST_ASSERT_INT_EQ(0, (int)trace.count);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 203U));
    module.monitor.state = DF_GVS_MONITOR_PUBLISHING;
    module.monitor.last_now_ms = 203U;
    module.monitor.media_ready = true;
    module.monitor.station_ipv4 = htonl(INADDR_LOOPBACK);
    memcpy(module.monitor.local, local, sizeof(local));
    memcpy(module.monitor.station, station, sizeof(station));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_push_jpeg(&module, station,
        local, htonl(INADDR_LOOPBACK), 2U, jpeg, sizeof(jpeg), 480U, 640U,
        204U));
    TEST_ASSERT_INT_EQ(1, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, kill(module.encoder.pid, SIGKILL));
    {
        int write_status = DF_OK;

        for (attempt = 0U; attempt < 100U && !module.encoder.encoder_exited;
             ++attempt) {
            write_status = df_media_module_push_jpeg(&module, station,
                local, htonl(INADDR_LOOPBACK), 2U, jpeg, sizeof(jpeg),
                480U, 640U, 205U + attempt);
            if (module.encoder.encoder_exited) break;
            {
                struct timespec delay = {0, 1000000L};
                (void)nanosleep(&delay, NULL);
            }
        }
        TEST_ASSERT_INT_EQ(DF_ERR_IO, write_status);
    }
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, module.monitor.state);
    TEST_ASSERT_INT_EQ(2, (int)module.monitor.generation);
    TEST_ASSERT_INT_EQ(0, module.queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 206U));
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, setenv("PATH", original_path, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(credentials_path));
    TEST_ASSERT_INT_EQ(0, unlink(encoder_program));
    TEST_ASSERT_INT_EQ(0, rmdir(encoder_directory));
}

void test_media_module_ignores_deprecated_relay_slots(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-deprecated-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = htonl(INADDR_LOOPBACK),
        .credentials_path = path,
        .deprecated_relay_url = "ftp://invalid.example",
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .deprecated_relay_send = module_deprecated_relay_send,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 0U));
    TEST_ASSERT_INT_EQ(1, module.credentials.rtsp_password[0] != '\0');
    TEST_ASSERT_INT_EQ(1, module.config.deprecated_relay_url == NULL);
    TEST_ASSERT_INT_EQ(1, module.callbacks.deprecated_relay_send == NULL);
    TEST_ASSERT_INT_EQ(0, (int)trace.deprecated_relay_calls);
    TEST_ASSERT_INT_EQ(1, module.initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_rejects_stale_commands_and_status_has_no_secrets(void) {
    struct df_media_module module = {0};
    struct df_media_module_status status = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-status-XXXXXX";
    struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 100));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_STOP, 99, false, 110));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(1, status.available);
    TEST_ASSERT_INT_EQ(0, strstr(status.state, "rtsp-secret") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(status.failure, "rtsp-secret") != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_owns_config_loads_credentials_and_redacts_status(void) {
    struct df_media_module module = {0};
    struct df_media_module_status status = {0};
    struct module_trace trace = {0};
    char host[] = "ha.local";
    char stream[] = "doorfast_preview";
    char username[] = "doorfast";
    char path[] = "/tmp/doorfast-media-module-XXXXXX";
    const char contents[] = "rtsp_password=rtsp-secret\n";
    int descriptor = mkstemp(path);
    struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .go2rtc_host = host,
        .go2rtc_port = 8554,
        .stream_name = stream,
        .rtsp_username = username,
        .credentials_path = path,
        .encoder = DF_MEDIA_ENCODER_SOFTWARE,
        .resolution = DF_MEDIA_RESOLUTION_SOURCE,
        .fps = 10,
        .bitrate_kbps = 800,
        .profile = DF_MEDIA_PROFILE_BASELINE,
        .deprecated_relay_url = "http://ha.local",
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_emit_control,
        .deprecated_relay_send = module_deprecated_relay_send,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(1, descriptor >= 0);
    TEST_ASSERT_INT_EQ(0, fchmod(descriptor, 0600));
    TEST_ASSERT_INT_EQ((int)sizeof(contents) - 1,
        (int)write(descriptor, contents, sizeof(contents) - 1U));
    TEST_ASSERT_INT_EQ(0, close(descriptor));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 100));
    memset(host, 'x', sizeof(host) - 1U);
    memset(stream, 'x', sizeof(stream) - 1U);
    memset(username, 'x', sizeof(username) - 1U);
    TEST_ASSERT_INT_EQ(0, strcmp("ha.local", module.config.go2rtc_host));
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_preview", module.config.stream_name));
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast", module.config.rtsp_username));
    TEST_ASSERT_INT_EQ(1, module.config.deprecated_relay_url == NULL);
    TEST_ASSERT_INT_EQ(1, module.callbacks.deprecated_relay_send == NULL);
    TEST_ASSERT_INT_EQ(0, strcmp("rtsp-secret",
        module.credentials.rtsp_password));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(0, strstr(status.state, "secret") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(status.failure, "secret") != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_restarts_after_failure_with_new_generation(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .credentials_path = "/tmp/doorfast-media-module-missing",
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_emit_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(&module, &config,
        &callbacks, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 2000));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 3000));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, module.monitor.state);
    TEST_ASSERT_INT_EQ(1, module.failure[0] != '\0');
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(&module, 4000));
    TEST_ASSERT_INT_EQ(2, (int)module.monitor.generation);
    TEST_ASSERT_INT_EQ(0, module.failure[0] != '\0');
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
}

void test_media_module_stop_ack_cleans_local_media(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    struct df_gvs_frame frame = {
        .destination = {0x61, 2, 1, 1, 1, 1},
        .source = {0x32, 2, 1, 0, 2, 0},
        .family = 0x03U,
        .opcode = 0x82U,
    };
    char path[] = "/tmp/doorfast-media-stop-ack-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304U,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(
        &module, &config, &callbacks, 100U));
    module_set_running_media(&module, 1U, 100U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_STOP, 1U, false, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_receive_control(
        &module, &frame, 0x01020304U, 102U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, module.monitor.state);
    TEST_ASSERT_INT_EQ(0, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, module.queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 102U));
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, memcmp(module.monitor.local, local, sizeof(local)));
    TEST_ASSERT_INT_EQ(0, memcmp(module.monitor.station, station, sizeof(station)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_stop_timeout_cleans_local_media(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-stop-timeout-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304U,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(
        &module, &config, &callbacks, 100U));
    module_set_running_media(&module, 1U, 100U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_STOP, 1U, false, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(
        &module, 101U + DF_GVS_MONITOR_STOP_TIMEOUT_MS));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, module.monitor.state);
    TEST_ASSERT_INT_EQ(0, module.encoder.running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, module.queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, strcmp("stop_timeout", module.failure));
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_start(
        &module, 102U + DF_GVS_MONITOR_STOP_TIMEOUT_MS));
    TEST_ASSERT_INT_EQ(2, (int)module.monitor.generation);
    TEST_ASSERT_INT_EQ(0, module.failure[0]);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_stop_cleanup_failure_is_not_reported_as_stopped(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    struct df_gvs_frame frame = {
        .destination = {0x61, 2, 1, 1, 1, 1},
        .source = {0x32, 2, 1, 0, 2, 0},
        .family = 0x03U,
        .opcode = 0x82U,
    };
    char path[] = "/tmp/doorfast-media-stop-failure-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304U,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(
        &module, &config, &callbacks, 100U));
    module_set_running_media(&module, 1U, 100U);
    module.stop_encoder = module_fail_encoder_stop;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_STOP, 1U, false, 101U));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_module_receive_control(
        &module, &frame, 0x01020304U, 102U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, module.monitor.state);
    TEST_ASSERT_INT_EQ(0, strcmp("encoder_exited", module.failure));
    TEST_ASSERT_INT_EQ(0, module.queue_initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, (int)trace.count);
    module.stop_encoder = df_media_encoder_stop;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_status_revision_tracks_public_snapshot_changes(void) {
    struct df_media_module module = {0};
    struct df_media_module_status status = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-revision-XXXXXX";
    const struct df_media_module_config_v2 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304U,
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v2 callbacks = {
        .emit_control = module_noop_control,
        .context = &trace,
    };
    const uint8_t jpeg[] = {0xff, 0xd8, 0x01, 0xff, 0xd9};
    unsigned index;

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_init(
        &module, &config, &callbacks, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(1, (int)status.status_revision);

    module_set_running_media(&module, 1U, 100U);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_VIEWER, 1U, true, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(2, (int)status.status_revision);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_VIEWING, status.monitor_state);
    TEST_ASSERT_INT_EQ(1, status.encoder_running ? 1 : 0);

    module.monitor.media_ready = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 102U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(2, (int)status.status_revision);

    module.encoder.running = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 103U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(3, (int)status.status_revision);
    module.encoder.running = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(&module, 104U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(4, (int)status.status_revision);

    for (index = 0; index <= DF_MEDIA_FRAME_QUEUE_CAPACITY; ++index) {
        TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&module.queue,
            jpeg, sizeof(jpeg), 1U, 105U + index));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_VIEWER, 1U, true, 110U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(5, (int)status.status_revision);
    TEST_ASSERT_INT_EQ(1, (int)status.queue_drops);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_VIEWER, 1U, true, 111U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(5, (int)status.status_revision);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_command(&module,
        DF_MEDIA_MODULE_COMMAND_STOP, 1U, false, 112U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(6, (int)status.status_revision);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_STOPPING, status.monitor_state);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_tick(
        &module, 112U + DF_GVS_MONITOR_STOP_TIMEOUT_MS));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(7, (int)status.status_revision);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, status.monitor_state);
    TEST_ASSERT_INT_EQ(0, status.encoder_running ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, strcmp("stop_timeout", status.failure));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}
