#include <string.h>

#include "gvs_monitor.h"
#include "test.h"

static int monitor_receive(struct df_gvs_monitor *monitor,
    const uint8_t source[6], const uint8_t destination[6], uint8_t opcode,
    const uint8_t *payload, size_t payload_length, uint32_t source_ipv4,
    uint64_t now_ms, struct df_gvs_monitor_result *result) {
    struct df_gvs_frame frame = {0};

    memcpy(frame.source, source, sizeof(frame.source));
    memcpy(frame.destination, destination, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = opcode;
    frame.payload = payload;
    frame.payload_length = (uint16_t)payload_length;
    return df_gvs_monitor_receive(monitor, &frame, source_ipv4, now_ms,
                                  result);
}

void test_gvs_monitor_retries_captured_request_and_accepts_confirmation(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    const uint8_t confirmation[] = {0x1e, 0x00, 0x01};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_action action = {0};
    struct df_gvs_monitor_result result = {0};

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(
        &monitor, local, station, 0x01020304U, 100U));
    TEST_ASSERT_INT_EQ(1, (int)monitor.generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 100U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x03, action.family);
    TEST_ASSERT_INT_EQ(0x04, action.opcode);
    TEST_ASSERT_INT_EQ(7, (int)action.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(action.payload,
        (const uint8_t[]){0x02, 0x20, 0x6f, 0x00, 0x20, 0x6e, 0x1e}, 7));
    TEST_ASSERT_INT_EQ(0, memcmp(action.destination, station, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(action.source, local, 6));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 1099U, &action));
    TEST_ASSERT_INT_EQ(0, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 1100U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x84,
        confirmation, sizeof(confirmation), 0x01020304U, 1150U, &result));
    TEST_ASSERT_INT_EQ(1, result.confirmed ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, monitor.state);
}

void test_gvs_monitor_retries_after_unconfirmed_response(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    const uint8_t other_station[6] = {0x32, 0x02, 0x01, 0x00, 0x03, 0x00};
    const uint8_t invalid_station[6] = {0x32, 0x02, 0x01, 0x01, 0x03, 0x00};
    const uint8_t confirmation[] = {0x1e, 0x00, 0x01};
    const uint8_t malformed_confirmation[] = {0x1e, 0x00, 0x00};
    const uint8_t malformed_unconfirmed[] = {0x00};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_action action = {0};
    struct df_gvs_monitor_result result = {0};

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_monitor_start(
        &monitor, local, invalid_station, 0x01020304U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(
        &monitor, local, station, 0x01020304U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 100U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, monitor_receive(&monitor, other_station,
        local, 0x84, confirmation, sizeof(confirmation), 0x01020304U, 101U,
        &result));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, monitor.state);
    TEST_ASSERT_INT_EQ(100, (int)monitor.last_now_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, monitor_receive(&monitor, station,
        local, 0x84, confirmation, sizeof(confirmation), 0x01020305U, 101U,
        &result));
    TEST_ASSERT_INT_EQ(100, (int)monitor.last_now_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, monitor_receive(&monitor, station,
        local, 0x84, malformed_confirmation, sizeof(malformed_confirmation),
        0x01020304U, 101U, &result));
    TEST_ASSERT_INT_EQ(100, (int)monitor.last_now_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, monitor_receive(&monitor, station,
        local, 0x50, malformed_unconfirmed, sizeof(malformed_unconfirmed),
        0x01020304U, 101U, &result));
    TEST_ASSERT_INT_EQ(100, (int)monitor.last_now_ms);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x50,
        NULL, 0U, 0x01020304U, 102U, &result));
    TEST_ASSERT_INT_EQ(0, result.failed ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, monitor.state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILURE_NONE, monitor.failure);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 1100U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x04, action.opcode);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x50,
        NULL, 0U, 0x01020304U, 1102U, &result));
    TEST_ASSERT_INT_EQ(0, result.failed ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, monitor.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 2100U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x04, action.opcode);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x84,
        confirmation, sizeof(confirmation), 0x01020304U, 2150U, &result));
    TEST_ASSERT_INT_EQ(1, result.confirmed ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, monitor.state);
}

void test_gvs_monitor_unconfirmed_responses_do_not_reset_request_limit(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_action action = {0};
    struct df_gvs_monitor_result result = {0};
    uint64_t now_ms = 100U;
    unsigned attempt;

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(
        &monitor, local, station, 0x01020304U, now_ms));
    for (attempt = 0U; attempt < DF_GVS_MONITOR_MAX_REQUESTS; attempt++) {
        TEST_ASSERT_INT_EQ(DF_OK,
            df_gvs_monitor_step(&monitor, now_ms, &action));
        TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
        TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local,
            0x50, NULL, 0U, 0x01020304U, now_ms + 2U, &result));
        TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, monitor.state);
        now_ms += DF_GVS_MONITOR_REQUEST_INTERVAL_MS;
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, now_ms, &action));
    TEST_ASSERT_INT_EQ(0, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, monitor.state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_TIMEOUT, monitor.failure);
}

void test_gvs_monitor_busy_response_retries_beyond_three_requests(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    const uint8_t confirmation[] = {0x1e, 0x00, 0x01};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_action action = {0};
    struct df_gvs_monitor_result result = {0};
    uint64_t now_ms = 100U;
    unsigned attempt;

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(
        &monitor, local, station, 0x01020304U, now_ms));
    for (attempt = 0U; attempt < 3U; attempt++) {
        TEST_ASSERT_INT_EQ(DF_OK,
            df_gvs_monitor_step(&monitor, now_ms, &action));
        TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
        TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local,
            0x50, NULL, 0U, 0x01020304U, now_ms + 2U, &result));
        now_ms += 1000U;
    }

    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_monitor_step(&monitor, now_ms, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x04, action.opcode);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x84,
        confirmation, sizeof(confirmation), 0x01020304U, now_ms + 2U,
        &result));
    TEST_ASSERT_INT_EQ(1, result.confirmed ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, monitor.state);
}

void test_gvs_monitor_admits_only_current_media_and_stops_locally_after_timeout(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t station[6] = {0x32, 0x02, 0x01, 0x00, 0x02, 0x00};
    const uint8_t confirmation[] = {0x1e, 0x00, 0x01};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_result result = {0};
    struct df_gvs_monitor_action action = {0};

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(
        &monitor, local, station, 0x01020304U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&monitor, station, local, 0x84,
        confirmation, sizeof(confirmation), 0x01020304U, 101U, &result));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_monitor_admit_jpeg(&monitor,
        station, local, 0x01020304U, monitor.generation + 1U, 102U, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_ADMIT_REJECT_GENERATION,
        result.admit_reject);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_admit_jpeg(&monitor, station,
        local, 0x01020304U, monitor.generation, 102U, &result));
    TEST_ASSERT_INT_EQ(1, result.media_ready ? 1 : 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_mark_publishing(&monitor,
        monitor.generation, 103U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_PUBLISHING, monitor.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_stop(&monitor,
        monitor.generation, 104U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 104U, &action));
    TEST_ASSERT_INT_EQ(1, action.send ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x02, action.opcode);
    TEST_ASSERT_INT_EQ(1, (int)action.payload_length);
    TEST_ASSERT_INT_EQ(0, action.payload[0]);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 1104U, &action));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, monitor.state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_STOP_TIMEOUT, monitor.failure);
}

void test_gvs_monitor_instances_reject_cross_station_and_stale_operations(void) {
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t main_station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t side_station[6] = {0x32U, 2U, 1U, 0U, 3U, 0U};
    const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    struct df_gvs_monitor main_monitor = {0};
    struct df_gvs_monitor side_monitor = {0};
    struct df_gvs_monitor_result result = {0};
    struct df_gvs_monitor_action action = {0};

    df_gvs_monitor_init(&main_monitor);
    df_gvs_monitor_init(&side_monitor);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start_with_generation(
        &main_monitor, local, main_station, 0x01020304U, 21U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start_with_generation(
        &side_monitor, local, side_station, 0x01020305U, 22U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&main_monitor, 100U,
        &action));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&side_monitor, 100U,
        &action));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, monitor_receive(&main_monitor,
        side_station, local, 0x84U, confirmation, sizeof(confirmation),
        0x01020305U, 101U, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_REQUESTING, main_monitor.state);
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&main_monitor, main_station,
        local, 0x84U, confirmation, sizeof(confirmation), 0x01020304U,
        101U, &result));
    TEST_ASSERT_INT_EQ(DF_OK, monitor_receive(&side_monitor, side_station,
        local, 0x84U, confirmation, sizeof(confirmation), 0x01020305U,
        102U, &result));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_monitor_admit_jpeg(
        &main_monitor, main_station, local, 0x01020304U, 20U, 103U,
        &result));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_cancel(&main_monitor, 104U));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_IDLE, main_monitor.state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, side_monitor.state);
}

void test_gvs_monitor_call_binding_enforces_first_frame_timeout(void) {
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    struct df_gvs_monitor monitor = {0};
    struct df_gvs_monitor_action action = {0};

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_monitor_set_first_frame_timeout(&monitor, 500U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_bind_call(&monitor, local,
        station, 0x01020304U, 7U, 100U));
    TEST_ASSERT_INT_EQ(600, (int)monitor.first_frame_deadline_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 599U, &action));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, monitor.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 600U, &action));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FAILED, monitor.state);
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT, monitor.failure);

    df_gvs_monitor_init(&monitor);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_monitor_bind_call(&monitor,
        local, station, 0x01020304U, 8U,
        UINT64_MAX - DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS + 1U));
}
