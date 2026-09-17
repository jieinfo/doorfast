#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gvs_frame.h"
#include "media_module.h"
#include "test.h"

struct module_trace {
    char entries[8][32];
    unsigned count;
};

static int module_write_credentials(char path[]) {
    static const char contents[] =
        "rtsp_password=rtsp-secret\nrelay_token=relay-secret\n";
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

static int module_write_encoder_stub(char directory[], char program[],
    size_t program_capacity) {
    static const char script[] = "#!/bin/sh\nIFS= read -r ignored || exit 0\n";
    int descriptor;

    if (mkdtemp(directory) == NULL ||
        snprintf(program, program_capacity, "%s/encoder", directory) >=
            (int)program_capacity)
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

static int module_send_relay(const char *url, const char *token,
    const char *json, void *context) {
    struct module_trace *trace = context;
    (void)url;
    (void)token;
    if (strstr(json, "monitor_preempted") != NULL) {
        (void)snprintf(trace->entries[trace->count++],
            sizeof(trace->entries[0]), "monitor_preempted");
    }
    return DF_OK;
}

void test_media_module_preempts_with_control_before_event(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-preempt-XXXXXX";
    struct df_media_module_config_v1 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .relay_url = "http://ha.local",
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v1 callbacks = {
        .emit_control = module_emit_control,
        .relay_send = module_send_relay,
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
    TEST_ASSERT_INT_EQ(3, (int)trace.count);
    TEST_ASSERT_INT_EQ(0, strcmp("monitor_preempted", trace.entries[2]));
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
    const struct df_media_module_config_v1 config = {
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
        .relay_url = "",
    };
    const struct df_media_module_callbacks_v1 callbacks = {
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

void test_media_module_clears_credentials_when_relay_initialization_fails(void) {
    struct df_media_module module = {0};
    char path[] = "/tmp/doorfast-media-relay-failure-XXXXXX";
    const struct df_media_module_config_v1 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = htonl(INADDR_LOOPBACK),
        .credentials_path = path,
        .relay_url = "ftp://invalid.example",
    };
    const struct df_media_module_callbacks_v1 callbacks = {
        .emit_control = module_noop_control,
    };

    TEST_ASSERT_INT_EQ(DF_OK, module_write_credentials(path));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_module_init(&module, &config,
        &callbacks, 0U));
    TEST_ASSERT_INT_EQ(0, module.credentials.rtsp_password[0]);
    TEST_ASSERT_INT_EQ(0, module.credentials.relay_token[0]);
    TEST_ASSERT_INT_EQ(0, module.initialized ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_rejects_stale_commands_and_status_has_no_secrets(void) {
    struct df_media_module module = {0};
    struct df_media_module_status status = {0};
    struct module_trace trace = {0};
    char path[] = "/tmp/doorfast-media-status-XXXXXX";
    struct df_media_module_config_v1 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .relay_url = "http://ha.local",
        .credentials_path = path,
    };
    const struct df_media_module_callbacks_v1 callbacks = {
        .emit_control = module_noop_control,
        .relay_send = module_send_relay,
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
    TEST_ASSERT_INT_EQ(0, strstr(status.state, "relay-secret") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(status.failure, "relay-secret") != NULL);
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
    char relay_url[] = "http://ha.local";
    char path[] = "/tmp/doorfast-media-module-XXXXXX";
    const char contents[] =
        "rtsp_password=rtsp-secret\nrelay_token=relay-secret\n";
    int descriptor = mkstemp(path);
    struct df_media_module_config_v1 config = {
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
        .relay_url = relay_url,
    };
    const struct df_media_module_callbacks_v1 callbacks = {
        .emit_control = module_emit_control,
        .relay_send = module_send_relay,
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
    memset(relay_url, 'x', sizeof(relay_url) - 1U);
    TEST_ASSERT_INT_EQ(0, strcmp("ha.local", module.config.go2rtc_host));
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_preview", module.config.stream_name));
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast", module.config.rtsp_username));
    TEST_ASSERT_INT_EQ(0, strcmp("http://ha.local", module.config.relay_url));
    TEST_ASSERT_INT_EQ(0, strcmp("rtsp-secret",
        module.credentials.rtsp_password));
    TEST_ASSERT_INT_EQ(0, strcmp("relay-secret",
        module.credentials.relay_token));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_status(&module, &status));
    TEST_ASSERT_INT_EQ(0, strstr(status.state, "secret") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(status.failure, "secret") != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_destroy(&module));
    TEST_ASSERT_INT_EQ(0, unlink(path));
}

void test_media_module_restarts_after_failure_with_new_generation(void) {
    struct df_media_module module = {0};
    struct module_trace trace = {0};
    const struct df_media_module_config_v1 config = {
        .enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1},
        .station = {0x32, 2, 1, 0, 2, 0},
        .station_ipv4 = 0x01020304,
        .relay_url = "",
        .credentials_path = "/tmp/doorfast-media-module-missing",
    };
    const struct df_media_module_callbacks_v1 callbacks = {
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
