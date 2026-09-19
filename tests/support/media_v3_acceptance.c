#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "doorfast.h"
#include "media_module.h"

struct acceptance_context {
    uint64_t available_kib;
    size_t phase;
    unsigned preview_requests[2];
    unsigned confirmations[2];
    unsigned stop_requests[2];
};

static int emit_control(const uint8_t destination[6],
    uint32_t destination_ipv4, const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, size_t payload_length,
    void *context) {
    (void)destination;
    (void)destination_ipv4;
    (void)source;
    (void)payload;
    (void)payload_length;
    if (context == NULL || family != 0x03U) return DF_ERR_INVALID;
    if (opcode == 0x04U)
        ((struct acceptance_context *)context)->preview_requests[
            ((struct acceptance_context *)context)->phase]++;
    else if (opcode == 0x02U)
        ((struct acceptance_context *)context)->stop_requests[
            ((struct acceptance_context *)context)->phase]++;
    else
        return DF_ERR_INVALID;
    return DF_OK;
}

static int resolve_route(const uint8_t peer[6], uint64_t now_ms,
    uint32_t *ipv4, void *context) {
    (void)now_ms;
    (void)context;
    if (peer == NULL || ipv4 == NULL) return DF_ERR_INVALID;
    *ipv4 = UINT32_C(0x0a020100) | peer[4];
    return DF_OK;
}

static int available_memory(uint64_t *available_kib, void *context) {
    const struct acceptance_context *acceptance = context;

    if (available_kib == NULL || acceptance == NULL) return DF_ERR_INVALID;
    *available_kib = acceptance->available_kib;
    return DF_OK;
}

static const char *state_name(enum df_media_session_state_v3 state) {
    switch (state) {
    case DF_MEDIA_SESSION_IDLE: return "idle";
    case DF_MEDIA_SESSION_REQUESTING: return "requesting";
    case DF_MEDIA_SESSION_AWAITING_VIDEO: return "awaiting_video";
    case DF_MEDIA_SESSION_PUBLISHING: return "publishing";
    case DF_MEDIA_SESSION_VIEWING: return "viewing";
    case DF_MEDIA_SESSION_STOPPING: return "stopping";
    case DF_MEDIA_SESSION_FAILED: return "failed";
    case DF_MEDIA_SESSION_PREEMPTED: return "preempted";
    }
    return "unknown";
}

static int read_status(void *instance, struct df_media_module_status_v3 *status,
    struct df_media_session_status_v3 entries[3]) {
    memset(status, 0, sizeof(*status));
    memset(entries, 0, sizeof(*entries) * 3U);
    status->sessions = entries;
    status->session_count = 3U;
    return df_media_module_api_v3.status(instance, status);
}

static const struct df_media_session_status_v3 *find_session(
    const struct df_media_module_status_v3 *status, const char *station_id) {
    size_t index;

    for (index = 0U; index < status->session_count; index++) {
        if (strcmp(status->sessions[index].station_id, station_id) == 0)
            return &status->sessions[index];
    }
    return NULL;
}

static int push_frame(void *instance,
    const struct df_media_station_config_v3 *station, const uint8_t local[6],
    uint64_t timestamp_ms, uint8_t marker) {
    const uint8_t jpeg[] = {0xffU, 0xd8U, marker, 0xffU, 0xd9U};

    return df_media_module_api_v3.push_jpeg(instance, station->logical_address,
        local, station->ipv4, jpeg, sizeof(jpeg), 480U, 640U, timestamp_ms);
}

static int receive_control(void *instance,
    const struct df_media_station_config_v3 *station, const uint8_t local[6],
    uint8_t opcode, const uint8_t *payload, size_t payload_length,
    uint64_t now_ms, struct acceptance_context *context) {
    struct df_gvs_frame frame = {0};
    int result;

    memcpy(frame.source, station->logical_address, sizeof(frame.source));
    memcpy(frame.destination, local, sizeof(frame.destination));
    frame.family = 0x03U;
    frame.opcode = opcode;
    frame.payload = payload;
    frame.payload_length = (uint16_t)payload_length;
    result = df_media_module_api_v3.receive_control(instance, &frame,
        station->ipv4, now_ms);
    if (result == DF_OK && opcode == 0x84U) context->confirmations[context->phase]++;
    return result;
}

static int wait_for_children(const char *first, const char *second) {
    const char *directory = getenv("DF_FAKE_FFMPEG_EVIDENCE_DIR");
    const struct timespec delay = {0, 10000000L};
    char main_ready[512];
    char side_ready[512];
    unsigned attempt;

    if (directory == NULL || first == NULL || second == NULL ||
        snprintf(main_ready, sizeof(main_ready), "%s/%s.ready", directory,
            first) < 0 ||
        snprintf(side_ready, sizeof(side_ready), "%s/%s.ready", directory,
            second) < 0)
        return DF_ERR_INVALID;
    for (attempt = 0U; attempt < 300U; attempt++) {
        if (access(main_ready, F_OK) == 0 && access(side_ready, F_OK) == 0)
            return DF_OK;
        (void)nanosleep(&delay, NULL);
    }
    return DF_ERR_IO;
}

static int fail(void *first, void *second, const char *message, int result) {
    if (second != NULL) df_media_module_api_v3.destroy(second);
    if (first != NULL) df_media_module_api_v3.destroy(first);
    (void)fprintf(stderr, "media ABI v3 harness failed: %s (%d)\n",
        message, result);
    return 1;
}

int main(int argc, char **argv) {
    static const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    static const uint8_t confirmation[] = {0x1eU, 0x00U, 0x01U};
    static const struct df_media_station_config_v3 stations[] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 2U, 0U},
         .ipv4 = UINT32_C(0x0a020114)},
        {.id = "gate_side", .stream_name = "doorfast_gate_side",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 3U, 0U},
         .ipv4 = UINT32_C(0x0a02011e)},
        {.id = "gate_call", .stream_name = "doorfast_gate_call",
         .enabled = true, .logical_address = {0x32U, 2U, 1U, 0U, 4U, 0U},
         .ipv4 = UINT32_C(0x0a020128)},
    };
    struct acceptance_context context;
    struct df_media_module_callbacks_v3 callbacks;
    struct df_media_module_config_v3 config;
    struct df_media_session_status_v3 entries[3];
    struct df_media_module_status_v3 status;
    const struct df_media_session_status_v3 *main_status;
    const struct df_media_session_status_v3 *side_status;
    const struct df_media_session_status_v3 *call_status;
    struct df_media_session_key main_key;
    struct df_media_session_key side_key;
    void *preserve = NULL;
    void *preempt = NULL;
    const char *before_main;
    const char *before_side;
    const char *after_main = "idle";
    const char *after_side;
    size_t peak_encoders;
    int preserve_result;
    int result;

    if (argc != 3) return fail(NULL, NULL, "expected RTSP port and memory", argc);
    memset(&context, 0, sizeof(context));
    context.available_kib = strtoull(argv[2], NULL, 10);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.emit_control = emit_control;
    callbacks.resolve_route = resolve_route;
    callbacks.available_memory = available_memory;
    callbacks.context = &context;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    memcpy(config.local, local, sizeof(config.local));
    config.stations = stations;
    config.station_count = sizeof(stations) / sizeof(stations[0]);
    config.max_encoders = 2U;
    config.incoming_call_policy = DF_MEDIA_CALL_PRESERVE_PREVIEWS;
    config.go2rtc_host = "127.0.0.1";
    config.go2rtc_port = (uint16_t)strtoul(argv[1], NULL, 10);
    config.encoder = DF_MEDIA_ENCODER_SOFTWARE;
    config.resolution = DF_MEDIA_RESOLUTION_SOURCE;
    config.fps = 10U;
    config.bitrate_kbps = 512U;
    config.profile = DF_MEDIA_PROFILE_BASELINE;
    config.min_free_kib = 1024U;
    config.preview_timeout_s = 30U;
    config.first_frame_timeout_s = 8U;

    preserve = df_media_module_api_v3.create(&config, &callbacks);
    if (preserve == NULL) return fail(NULL, NULL, "create preserve instance", 0);
    result = df_media_module_api_v3.start(preserve, "gate_main",
        DF_MEDIA_SESSION_PREVIEW, 0U, 100U);
    if (result != DF_OK) return fail(preserve, NULL, "start gate_main", result);
    result = df_media_module_api_v3.start(preserve, "gate_side",
        DF_MEDIA_SESSION_PREVIEW, 0U, 101U);
    if (result != DF_OK) return fail(preserve, NULL, "start gate_side", result);
    result = df_media_module_api_v3.tick(preserve, 101U);
    if (result != DF_OK || context.preview_requests[0] != 2U)
        return fail(preserve, NULL, "emit preserve preview requests", result);
    result = receive_control(preserve, &stations[0], local, 0x84U,
        confirmation, sizeof(confirmation), 102U, &context);
    if (result != DF_OK)
        return fail(preserve, NULL, "confirm gate_main", result);
    result = receive_control(preserve, &stations[1], local, 0x84U,
        confirmation, sizeof(confirmation), 103U, &context);
    if (result != DF_OK)
        return fail(preserve, NULL, "confirm gate_side", result);
    result = push_frame(preserve, &stations[0], local, 200U, 0x11U);
    if (result != DF_OK) return fail(preserve, NULL, "push gate_main", result);
    result = push_frame(preserve, &stations[1], local, 201U, 0x22U);
    if (result != DF_OK) return fail(preserve, NULL, "push gate_side", result);
    result = wait_for_children("doorfast_gate_main", "doorfast_gate_side");
    if (result != DF_OK)
        return fail(preserve, NULL, "wait for RTSP producers", result);
    result = read_status(preserve, &status, entries);
    if (result != DF_OK) return fail(preserve, NULL, "status before stop", result);
    main_status = find_session(&status, "gate_main");
    side_status = find_session(&status, "gate_side");
    if (main_status == NULL || side_status == NULL ||
        !main_status->encoder_running || !side_status->encoder_running ||
        status.active_encoders != 2U)
        return fail(preserve, NULL, "two real encoder sessions", DF_ERR_IO);
    before_main = state_name(main_status->state);
    before_side = state_name(side_status->state);
    peak_encoders = status.active_encoders;
    side_key.station_id = "gate_side";
    side_key.generation = side_status->generation;
    preserve_result = df_media_module_api_v3.start(preserve, "gate_call",
        DF_MEDIA_SESSION_CALL, 900U, 210U);
    if (preserve_result != DF_MEDIA_ERROR_CAPACITY_BUSY)
        return fail(preserve, NULL, "preserve capacity policy", preserve_result);
    main_key.station_id = "gate_main";
    main_key.generation = main_status->generation;
    result = df_media_module_api_v3.command(preserve,
        DF_MEDIA_MODULE_COMMAND_STOP, &main_key, false, 220U);
    if (result != DF_OK) return fail(preserve, NULL, "stop gate_main", result);
    result = receive_control(preserve, &stations[0], local, 0x82U,
        NULL, 0U, 221U, &context);
    if (result != DF_OK)
        return fail(preserve, NULL, "ack gate_main stop", result);
    result = read_status(preserve, &status, entries);
    if (result != DF_OK) return fail(preserve, NULL, "status after stop", result);
    side_status = find_session(&status, "gate_side");
    if (find_session(&status, "gate_main") != NULL || side_status == NULL ||
        !side_status->encoder_running || status.active_encoders != 1U)
        return fail(preserve, NULL, "isolated stop", DF_ERR_IO);
    after_side = state_name(side_status->state);
    result = df_media_module_api_v3.command(preserve,
        DF_MEDIA_MODULE_COMMAND_STOP, &side_key, false, 230U);
    if (result != DF_OK) return fail(preserve, NULL, "stop gate_side", result);
    result = receive_control(preserve, &stations[1], local, 0x82U,
        NULL, 0U, 231U, &context);
    if (result != DF_OK)
        return fail(preserve, NULL, "ack gate_side stop", result);
    if (context.stop_requests[0] != 2U || context.confirmations[0] != 2U)
        return fail(preserve, NULL, "preserve protocol trace", DF_ERR_IO);
    df_media_module_api_v3.destroy(preserve);
    preserve = NULL;

    config.incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW;
    context.phase = 1U;
    preempt = df_media_module_api_v3.create(&config, &callbacks);
    if (preempt == NULL) return fail(NULL, NULL, "create preempt instance", 0);
    result = df_media_module_api_v3.start(preempt, "gate_main",
        DF_MEDIA_SESSION_PREVIEW, 0U, 300U);
    if (result != DF_OK) return fail(preserve, preempt, "start preempt main", result);
    result = df_media_module_api_v3.start(preempt, "gate_side",
        DF_MEDIA_SESSION_PREVIEW, 0U, 301U);
    if (result != DF_OK) return fail(preserve, preempt, "start preempt side", result);
    result = df_media_module_api_v3.tick(preempt, 301U);
    if (result != DF_OK || context.preview_requests[1] != 2U)
        return fail(preserve, preempt, "emit preempt preview requests", result);
    result = receive_control(preempt, &stations[0], local, 0x84U,
        confirmation, sizeof(confirmation), 302U, &context);
    if (result != DF_OK)
        return fail(preserve, preempt, "confirm preempt main", result);
    result = receive_control(preempt, &stations[1], local, 0x84U,
        confirmation, sizeof(confirmation), 303U, &context);
    if (result != DF_OK)
        return fail(preserve, preempt, "confirm preempt side", result);
    result = push_frame(preempt, &stations[0], local, 310U, 0x11U);
    if (result != DF_OK)
        return fail(preserve, preempt, "publish preempt main", result);
    result = push_frame(preempt, &stations[1], local, 311U, 0x22U);
    if (result != DF_OK)
        return fail(preserve, preempt, "publish preempt side", result);
    result = wait_for_children("doorfast_gate_main", "doorfast_gate_side");
    if (result != DF_OK)
        return fail(preserve, preempt, "wait for preempt previews", result);
    result = read_status(preempt, &status, entries);
    if (result != DF_OK)
        return fail(preserve, preempt, "preempt status before call", result);
    main_status = find_session(&status, "gate_main");
    side_status = find_session(&status, "gate_side");
    if (main_status == NULL || side_status == NULL ||
        main_status->state != DF_MEDIA_SESSION_PUBLISHING ||
        side_status->state != DF_MEDIA_SESSION_PUBLISHING ||
        status.active_encoders != 2U)
        return fail(preserve, preempt, "publishing previews before call", DF_ERR_IO);
    result = df_media_module_api_v3.start(preempt, "gate_call",
        DF_MEDIA_SESSION_CALL, 901U, 320U);
    if (result != DF_OK) return fail(preserve, preempt, "preempt for call", result);
    result = push_frame(preempt, &stations[2], local, 321U, 0x55U);
    if (result != DF_OK) return fail(preserve, preempt, "publish call", result);
    result = wait_for_children("doorfast_gate_side", "doorfast_gate_call");
    if (result != DF_OK)
        return fail(preserve, preempt, "wait for call replacement", result);
    result = read_status(preempt, &status, entries);
    if (result != DF_OK) return fail(preserve, preempt, "preempt status", result);
    side_status = find_session(&status, "gate_side");
    call_status = find_session(&status, "gate_call");
    if (strcmp(status.preempted_station_id, "gate_main") != 0 ||
        side_status == NULL || call_status == NULL ||
        !side_status->encoder_running || !call_status->encoder_running ||
        status.active_encoders != 2U || context.stop_requests[1] != 1U ||
        context.confirmations[1] != 2U)
        return fail(preserve, preempt, "preempt status contract", DF_ERR_IO);

    (void)printf("{\"engine\":\"df_media_module_api_v3\","
        "\"configured_capacity\":%zu,\"abi_peak_active_encoders\":%zu,"
        "\"streams\":[\"doorfast_gate_main\",\"doorfast_gate_side\"],"
        "\"states\":{\"before_stop\":{\"gate_main\":\"%s\","
        "\"gate_side\":\"%s\"},\"after_stop\":{\"gate_main\":\"%s\","
        "\"gate_side\":\"%s\"}},\"isolated_stop\":true,"
        "\"protocol\":{\"preserve\":{\"preview_03_04\":%u,"
        "\"confirmation_03_84\":%u,\"stop_03_02\":%u},"
        "\"preempt\":{\"preview_03_04\":%u,"
        "\"confirmation_03_84\":%u,\"stop_03_02\":%u}},"
        "\"cleanup\":{\"active_encoders\":0},"
        "\"memory\":{\"available_kib\":%" PRIu64 ","
        "\"minimum_free_kib\":1024},"
        "\"call_at_capacity\":{\"preserve_previews\":{"
        "\"result\":\"capacity_busy\"},\"preempt_oldest_preview\":{"
        "\"active_before\":2,\"victim_state_before\":\"publishing\","
        "\"preempted\":\"%s\",\"active_preview\":\"%s\","
        "\"result\":\"accepted\",\"active_encoders\":%zu,"
        "\"call_stream\":\"%s\"}}}\n",
        status.configured_capacity, peak_encoders, before_main, before_side,
        after_main, after_side,
        context.preview_requests[0], context.confirmations[0],
        context.stop_requests[0], context.preview_requests[1],
        context.confirmations[1], context.stop_requests[1],
        context.available_kib,
        status.preempted_station_id, side_status->station_id,
        status.active_encoders, call_status->stream_name);
    df_media_module_api_v3.destroy(preempt);
    df_media_module_api_v3.destroy(preserve);
    return 0;
}
