#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "capture.h"
#include "gvs_memory_sender.h"
#include "gvs_serialize.h"
#include "runtime_service.h"
#include "runtime_ubus.h"
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
    unsigned start_calls;
    unsigned control_calls;
    bool started_while_idle;
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    enum df_media_session_purpose purpose;
    uint64_t request_generation;
    int start_result;
    char preempted_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t preempted_generation;
    unsigned video_calls;
    int video_result;
    unsigned tick_calls;
    char failed_station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t failed_generation;
    enum df_media_error_v3 failed_error;
};

static int runtime_media_fake_start(void *instance, const char *station_id,
    enum df_media_session_purpose purpose, uint64_t request_generation,
    uint64_t now_ms) {
    struct runtime_media_trace *trace = instance;
    (void)now_ms;
    trace->start_calls++;
    trace->started_while_idle = trace->session->state == DF_GVS_IDLE;
    (void)snprintf(trace->station_id, sizeof(trace->station_id), "%s",
        station_id);
    trace->purpose = purpose;
    trace->request_generation = request_generation;
    return trace->start_result;
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

static int runtime_media_fake_status(const void *instance,
    struct df_media_module_status_v3 *status) {
    const struct runtime_media_trace *trace = instance;

    (void)snprintf(status->preempted_station_id,
        sizeof(status->preempted_station_id), "%s",
        trace->preempted_station_id);
    status->preempted_generation = trace->preempted_generation;
    (void)snprintf(status->failed_station_id,
        sizeof(status->failed_station_id), "%s", trace->failed_station_id);
    status->failed_generation = trace->failed_generation;
    status->failed_error = trace->failed_error;
    status->required_session_count = 0U;
    status->session_count = 0U;
    return DF_OK;
}

static int runtime_media_fake_tick(void *instance, uint64_t now_ms) {
    struct runtime_media_trace *trace = instance;

    (void)now_ms;
    trace->tick_calls++;
    return DF_OK;
}

static int runtime_media_fake_video(void *instance,
    const struct df_gvs_video_packet *packet, uint32_t source_ipv4,
    uint64_t now_ms) {
    struct runtime_media_trace *trace = instance;

    (void)packet;
    (void)source_ipv4;
    (void)now_ms;
    trace->video_calls++;
    return trace->video_result;
}

void test_runtime_media_builds_module_config_without_guessing_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_station registry_stations[2] = {
        {
            .id = "gate_main", .stream_name = "doorfast_gate_main",
            .logical_address = {0x32, 2, 1, 0, 2, 0}, .enabled = true,
        },
        {
            .id = "gate_side", .stream_name = "doorfast_gate_side",
            .logical_address = {0x32, 2, 1, 0, 3, 0}, .enabled = true,
        },
    };
    struct df_runtime_config runtime = {0};
    struct df_media_station_config_v3 stations[2];
    struct df_media_module_config_v3 output;
    uint32_t loopback = 0;

    runtime.stations.items = registry_stations;
    runtime.stations.count = 2U;
    runtime.config.media.enabled = true;
    runtime.config.media.go2rtc_host = "ha.local";
    runtime.config.media.rtsp_username = "doorfast";
    runtime.config.media.credentials_path = DF_MEDIA_CREDENTIALS_PATH;
    runtime.config.media.max_encoders = 2U;
    runtime.config.media.overload_policy =
        DF_MEDIA_OVERLOAD_STOP_OLDEST_PREVIEW;
    runtime.config.media.min_free_kib = 262144U;
    runtime.config.media.preview_timeout_s = 90U;
    runtime.config.media.first_frame_timeout_s = 6U;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_build_module_config(
        &runtime, local, stations, 2U, &output));
    TEST_ASSERT_INT_EQ(0, memcmp(local, output.local, sizeof(output.local)));
    TEST_ASSERT_INT_EQ(0, memcmp((const uint8_t[]){0x32, 2, 1, 0, 2, 0},
                                 stations[0].logical_address,
                                 sizeof(stations[0].logical_address)));
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", stations[0].id));
    TEST_ASSERT_INT_EQ(0, strcmp("doorfast_gate_side",
        stations[1].stream_name));
    TEST_ASSERT_INT_EQ(2, (int)output.station_count);
    TEST_ASSERT_INT_EQ(2, (int)output.max_encoders);
    TEST_ASSERT_INT_EQ(DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW,
        output.incoming_call_policy);
    TEST_ASSERT_INT_EQ(262144, (int)output.min_free_kib);
    TEST_ASSERT_INT_EQ(90, (int)output.preview_timeout_s);
    TEST_ASSERT_INT_EQ(6, (int)output.first_frame_timeout_s);

    TEST_ASSERT_INT_EQ(1, inet_pton(AF_INET, "127.0.0.1", &loopback));
    registry_stations[1].configured_ipv4 = loopback;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_build_module_config(
        &runtime, local, stations, 2U, &output));
    TEST_ASSERT_INT_EQ((int)loopback, (int)stations[1].ipv4);
}

void test_runtime_media_preempts_before_incoming_call_state_changes(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct runtime_media_trace trace = {
        .session = &session,
        .preempted_generation = 8U,
    };
    struct df_station configured_station = {
        .id = "gate_main", .logical_address = {0x32, 2, 1, 0, 2, 0},
        .enabled = true,
    };
    const struct df_station_registry stations = {
        .items = &configured_station, .count = 1U,
    };
    const struct df_media_module_api_v3 api = {
        .start = runtime_media_fake_start,
        .receive_control = runtime_media_fake_control,
        .status = runtime_media_fake_status,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    struct df_runtime_ubus ubus = {0};
    struct df_runtime_log_entry log_entry;
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE];
    size_t packet_length = 0;
    int media_result = DF_ERR_INVALID;

    (void)snprintf(trace.preempted_station_id,
        sizeof(trace.preempted_station_id), "%s", "gate_side");

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, local, station, 0x03, 0x01, NULL, 0,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(
        &media, &ubus, NULL, &stations, &control, packet, packet_length, local,
        &session,
        &deadline,
        htonl(0x7f000001U), 100, &result, &media_result));
    TEST_ASSERT_INT_EQ(DF_OK, media_result);
    TEST_ASSERT_INT_EQ(1, (int)trace.start_calls);
    TEST_ASSERT_INT_EQ(1, trace.started_while_idle);
    TEST_ASSERT_INT_EQ(0, strcmp("gate_main", trace.station_id));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL, trace.purpose);
    TEST_ASSERT_INT_EQ((int)session.generation,
        (int)trace.request_generation);
    TEST_ASSERT_INT_EQ(0, (int)trace.control_calls);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.accepted_call);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 0U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=monitor_preempted station_id=gate_side generation=8",
        log_entry.message));
}

void test_runtime_media_capacity_busy_does_not_block_call_controls(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct runtime_media_trace trace = {
        .session = &session,
        .start_result = DF_MEDIA_ERROR_CAPACITY_BUSY,
    };
    struct df_station configured_station = {
        .id = "gate_main", .logical_address = {0x32, 2, 1, 0, 2, 0},
        .enabled = true,
    };
    const struct df_station_registry stations = {
        .items = &configured_station, .count = 1U,
    };
    const struct df_media_module_api_v3 api = {
        .start = runtime_media_fake_start,
        .receive_control = runtime_media_fake_control,
        .status = runtime_media_fake_status,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    struct df_runtime_ubus ubus = {0};
    struct df_runtime_log_entry log_entry;
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE];
    size_t packet_length = 0U;
    int media_result = DF_OK;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, local, station, 0x03U, 0x01U, NULL, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(
        &media, &ubus, NULL, &stations, &control, packet, packet_length, local,
        &session,
        &deadline,
        htonl(0x7f000001U), 100U, &result, &media_result));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY, media_result);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.accepted_call);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(&control,
        &session, session.generation, local, 8303U, 8302U, 120U, 101U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 0U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=incoming_call_media_capacity_busy station_id=gate_main "
        "generation=1 error=capacity_busy", log_entry.message));
}

void test_runtime_media_forwards_preview_keepalive_control(void) {
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct runtime_media_trace trace = {0};
    const struct df_media_module_api_v3 api = {
        .receive_control = runtime_media_fake_control,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE];
    size_t packet_length = 0U;
    int media_result = DF_ERR_INVALID;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, station, local, 0x03U, 0x51U, NULL, 0U,
        df_gvs_placeholder_header_fields, NULL));
    (void)df_runtime_receive_control_with_media(
        &media, NULL, NULL, NULL, &control, packet, packet_length, local,
        &session, &deadline, htonl(0x0a054000U), 100U, &result,
        &media_result);
    TEST_ASSERT_INT_EQ(1, (int)trace.control_calls);
}

void test_runtime_media_video_failure_is_logged_with_station_identity(void) {
    struct df_station station = {
        .id = "gate_main",
        .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
        .enabled = true,
    };
    const struct df_station_registry stations = {
        .items = &station,
        .count = 1U,
    };
    struct runtime_media_trace trace = {.video_result = DF_ERR_IO};
    const struct df_media_module_api_v3 api = {
        .push_video = runtime_media_fake_video,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    struct df_runtime_ubus ubus = {0};
    struct df_runtime_log_entry log_entry;
    struct df_gvs_video_packet packet = {0};

    memcpy(packet.source, station.logical_address, sizeof(packet.source));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_media_push_video_with_event(
        &media, &ubus, &stations, &packet, 0x01020304U, 900U));
    TEST_ASSERT_INT_EQ(1, (int)trace.video_calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 0U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=media_pipeline_failed station_id=gate_main "
        "error=encoder_failed", log_entry.message));

    memset(&ubus, 0, sizeof(ubus));
    trace.video_result = DF_ERR_INVALID;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_media_push_video_with_event(
        &media, &ubus, &stations, &packet, 0x01020304U, 901U));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_log_get(&ubus, 0U, &log_entry));
}

void test_runtime_media_async_failure_is_logged_once_with_station_identity(void) {
    struct runtime_media_trace trace = {
        .failed_generation = 12U,
        .failed_error = DF_MEDIA_ERROR_ENCODER_FAILED,
    };
    const struct df_media_module_api_v3 api = {
        .tick = runtime_media_fake_tick,
        .status = runtime_media_fake_status,
    };
    struct df_runtime_media_module media = {
        .api = &api,
        .instance = &trace,
        .available = true,
    };
    struct df_runtime_ubus ubus = {0};
    struct df_runtime_log_entry log_entry;

    (void)snprintf(trace.failed_station_id, sizeof(trace.failed_station_id),
        "%s", "gate_side");
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_tick_with_event(&media, &ubus, NULL, 900U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_tick_with_event(&media, &ubus, NULL, 901U));
    TEST_ASSERT_INT_EQ(2, (int)trace.tick_calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 0U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=media_pipeline_failed station_id=gate_side generation=12 "
        "error=encoder_failed", log_entry.message));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_log_get(&ubus, 1U, &log_entry));
}
