#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gvs_memory_sender.h"
#include "gvs_serialize.h"
#include "runtime_service.h"
#include "test.h"

struct delay_trace {
    unsigned values[20];
    size_t count;
    bool fail_second;
};

struct station_scan_trace {
    struct df_gvs_station_scan_action actions[3];
    size_t count;
    size_t attempts;
    bool fail_first;
};

struct station_scan_wait_trace {
    struct df_gvs_station_scan *scan;
    struct station_scan_trace *emissions;
    uint64_t now_ms;
};

static int record_station_scan(
    const struct df_gvs_station_scan_action *action, void *context) {
    struct station_scan_trace *trace = context;

    if (action == NULL || trace == NULL || trace->attempts >= 4U)
        return DF_ERR_INVALID;
    trace->attempts++;
    if (trace->fail_first && trace->attempts == 1U)
        return DF_ERR_IO;
    if (trace->count >= 3U) return DF_ERR_INVALID;
    trace->actions[trace->count++] = *action;
    return DF_OK;
}

static int pump_station_scan_slice(unsigned delay_ms, void *context) {
    struct station_scan_wait_trace *trace = context;

    if (trace == NULL) return DF_ERR_INVALID;
    trace->now_ms += delay_ms;
    return df_runtime_station_scan_service(true, trace->scan, trace->now_ms,
        record_station_scan, trace->emissions);
}

static int record_delay_slice(unsigned delay_ms, void *context) {
    struct delay_trace *trace = context;

    if (trace == NULL || trace->count >= 20U) {
        return DF_ERR_INVALID;
    }
    trace->values[trace->count++] = delay_ms;
    if (trace->fail_second && trace->count == 2U) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

void test_runtime_delay_is_pumped_in_bounded_slices(void) {
    struct delay_trace trace = {0};
    struct station_scan_trace scan_trace = {0};
    struct df_gvs_station_scan scan = {0};
    const uint8_t identity[6] = {0x61, 0x02, 0x01, 1, 1, 1};

    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_runtime_pump_delay(1000, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(4, (int)trace.count);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[0]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[1]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[2]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[3]);

    memset(&trace, 0, sizeof(trace));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_runtime_pump_delay(251, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[0]);
    TEST_ASSERT_INT_EQ(1, (int)trace.values[1]);

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_station_scan_start(&scan, identity, 1000U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 1000U, record_station_scan, &scan_trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 1499U, record_station_scan, &scan_trace));
    TEST_ASSERT_INT_EQ(1, (int)scan_trace.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 1500U, record_station_scan, &scan_trace));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 2000U, record_station_scan, &scan_trace));
    TEST_ASSERT_INT_EQ(3, (int)scan_trace.count);
    TEST_ASSERT_INT_EQ(0x07, scan_trace.actions[0].family);
    TEST_ASSERT_INT_EQ(0x06, scan_trace.actions[0].opcode);
}

void test_runtime_delay_rejects_invalid_input_and_stops_on_failure(void) {
    struct delay_trace trace = {.fail_second = true};

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_pump_delay(0, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_pump_delay(250, 0, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_pump_delay(250, 250, NULL, &trace));
    TEST_ASSERT_INT_EQ(
        DF_ERR_IO,
        df_runtime_pump_delay(500, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
}

void test_runtime_retry_wait_slices_keep_station_scan_progressing(void) {
    const uint8_t identity[6] = {0x61, 0x02, 0x01, 1, 1, 1};
    struct df_gvs_station_scan scan = {0};
    struct station_scan_trace emissions = {0};
    struct station_scan_wait_trace wait = {
        .scan = &scan,
        .emissions = &emissions,
        .now_ms = 1000U,
    };

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_station_scan_start(&scan, identity, wait.now_ms));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_service(true, &scan,
        wait.now_ms, record_station_scan, &emissions));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_pump_delay(
        1000U, 250U, pump_station_scan_slice, &wait));
    TEST_ASSERT_INT_EQ(3, (int)emissions.count);
    TEST_ASSERT_INT_EQ(3, (int)emissions.attempts);
    TEST_ASSERT_INT_EQ(0, scan.active ? 1 : 0);
}

void test_runtime_station_scan_retries_failed_emit_without_consuming_frame(void) {
    const uint8_t identity[6] = {0x61, 0x02, 0x01, 1, 1, 1};
    struct df_gvs_station_scan scan = {0};
    struct station_scan_trace emissions = {.fail_first = true};

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_station_scan_start(&scan, identity, 1000U));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_station_scan_tick(
        &scan, 1000U, record_station_scan, &emissions));
    TEST_ASSERT_INT_EQ(0, (int)scan.emitted);
    TEST_ASSERT_INT_EQ(1, scan.active ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 1001U, record_station_scan, &emissions));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 1500U, record_station_scan, &emissions));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_station_scan_tick(
        &scan, 2000U, record_station_scan, &emissions));
    TEST_ASSERT_INT_EQ(4, (int)emissions.attempts);
    TEST_ASSERT_INT_EQ(3, (int)emissions.count);
    TEST_ASSERT_INT_EQ(3, (int)scan.emitted);
    TEST_ASSERT_INT_EQ(0, scan.active ? 1 : 0);
}

struct runtime_media_trace {
    struct df_gvs_session *session;
    unsigned preempt_calls;
    unsigned control_calls;
    bool preempted_while_idle;
};

static int runtime_media_fake_preempt(void *instance, uint64_t now_ms) {
    struct runtime_media_trace *trace = instance;
    (void)now_ms;
    trace->preempt_calls++;
    trace->preempted_while_idle = trace->session->state == DF_GVS_IDLE;
    return DF_OK;
}

static int runtime_media_fake_control(void *instance,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms) {
    struct runtime_media_trace *trace = instance;
    (void)frame;
    (void)source_ipv4;
    (void)now_ms;
    trace->control_calls++;
    return DF_OK;
}

void test_runtime_media_builds_module_config_without_guessing_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_runtime_config runtime = {0};
    struct df_media_module_config_v2 output;
    uint32_t loopback = 0;

    runtime.config.media.enabled = true;
    runtime.config.media.station_address = "32:02:01:00:02:00";
    runtime.config.media.station_ipv4 = "";
    runtime.config.media.go2rtc_host = "ha.local";
    runtime.config.media.stream_name = "doorfast_preview";
    runtime.config.media.rtsp_username = "doorfast";
    runtime.config.media.credentials_path = DF_MEDIA_CREDENTIALS_PATH;
    runtime.config.media.relay_url = "";
    runtime.config.media.min_free_kib = 262144U;
    runtime.config.media.preview_timeout_s = 90U;
    runtime.config.media.first_frame_timeout_s = 6U;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_build_module_config(
        &runtime, local, &output));
    TEST_ASSERT_INT_EQ(0, (int)output.station_ipv4);
    TEST_ASSERT_INT_EQ(0, memcmp(local, output.local, sizeof(output.local)));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x32, 2, 1, 0, 2, 0},
                                 output.station, sizeof(output.station)));
    TEST_ASSERT_INT_EQ(262144, (int)output.min_free_kib);
    TEST_ASSERT_INT_EQ(90, (int)output.preview_timeout_s);
    TEST_ASSERT_INT_EQ(6, (int)output.first_frame_timeout_s);

    runtime.config.media.station_ipv4 = "127.0.0.1";
    TEST_ASSERT_INT_EQ(1, inet_pton(AF_INET, "127.0.0.1", &loopback));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_build_module_config(
        &runtime, local, &output));
    TEST_ASSERT_INT_EQ((int)loopback, (int)output.station_ipv4);
}

void test_runtime_media_preempts_before_incoming_call_state_changes(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct runtime_media_trace trace = {.session = &session};
    const struct df_media_module_api_v2 api = {
        .receive_control = runtime_media_fake_control,
        .preempt = runtime_media_fake_preempt,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE];
    size_t packet_length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, local, station, 0x03, 0x01, NULL, 0,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(
        &media, &control, packet, packet_length, local, &session, &deadline,
        htonl(0x7f000001U), 100, &result));
    TEST_ASSERT_INT_EQ(1, (int)trace.preempt_calls);
    TEST_ASSERT_INT_EQ(1, trace.preempted_while_idle);
    TEST_ASSERT_INT_EQ(0, (int)trace.control_calls);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.accepted_call);
}
