#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "capture.h"
#include "gvs_memory_sender.h"
#include "gvs_serialize.h"
#include "gvs_audio_playout.h"
#include "gvs_audio_chunk_store.h"
#include "gvs_media.h"
#include "gvs_packet.h"
#include "gvs_media_receiver.h"
#include "g711_alaw.h"
#include "runtime_service.h"
#include "runtime_ubus.h"
#include "test.h"

void test_runtime_call_submit_updates_live_owner(void) {
    const uint8_t identity[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_station station = {
        .id = "gate_main", .logical_address = {0x32, 2, 1, 0, 2, 0},
    };
    const struct df_station_registry stations = {.items = &station, .count = 1U};
    struct df_gvs_call_control control;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_runtime_call_binding binding;
    struct df_runtime_call_request request = {
        .type = DF_GVS_CALL_COMMAND_CALL, .session_generation = 9U,
        .primary_media_port = 8303, .secondary_media_port = 8302,
        .duration_seconds = 120,
    };
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0U,
        df_gvs_placeholder_header_fields, NULL));
    (void)snprintf(request.station_id, sizeof(request.station_id), "%s", "gate_main");
    binding.control = &control; binding.session = &session;
    binding.deadline = &deadline; binding.identity = identity;
    binding.stations = &stations;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_call_submit(&request, 0U, &binding));
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    TEST_ASSERT_INT_EQ(9, (int)session.generation);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, station.logical_address, 6U));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_QUEUED, control.dispatch.state);
}

struct runtime_audio_sink {
    struct df_gvs_audio_chunk chunk;
    uint8_t samples[1600];
    unsigned attempts;
    int failure;
};

static int runtime_audio_publish(const uint8_t *samples,
    const struct df_gvs_audio_chunk *chunk, void *context) {
    struct runtime_audio_sink *sink = context;
    sink->attempts++;
    sink->chunk = *chunk;
    if (chunk->sample_count > sizeof(sink->samples)) return -1;
    memcpy(sink->samples, samples, chunk->sample_count);
    return sink->failure;
}

static int runtime_audio_emit(const struct df_gvs_audio_emission *emission,
    void *context) {
    return df_gvs_audio_playout_push(context, emission);
}

static void runtime_audio_init(struct df_gvs_audio_buffer *buffer,
    struct df_gvs_audio_playout *playout, struct df_gvs_audio_jitter *jitter,
    struct runtime_audio_sink *sink) {
    df_gvs_audio_buffer_init(buffer);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(playout, buffer,
        runtime_audio_publish, sink));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_init(jitter,
        runtime_audio_emit, playout));
}

/* Catches a missing idle tick, reversed tick ordering and retries that consume
 * the cursor or run before the publisher's retry deadline. */
void test_runtime_audio_service_advances_deadlines_and_retries(void) {
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_playout playout;
    struct df_gvs_audio_jitter jitter;
    struct runtime_audio_sink sink = {0};
    uint8_t samples[160];
    memset(samples, 0x55, sizeof(samples));
    runtime_audio_init(&buffer, &playout, &jitter, &sink);
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 200));
    TEST_ASSERT_INT_EQ(0, sink.attempts);
    df_gvs_audio_jitter_reset(&jitter, 7);
    df_gvs_audio_playout_reset(&playout, 7);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 10, 7, 300));
    memset(samples, 0x66, sizeof(samples));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 12, 7, 320));
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 359));
    TEST_ASSERT_INT_EQ(1, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 400));
    TEST_ASSERT_INT_EQ(1, sink.attempts);
    TEST_ASSERT_INT_EQ(480, (int)sink.chunk.sample_count);
    TEST_ASSERT_INT_EQ(2, (int)sink.chunk.revision);
    TEST_ASSERT_INT_EQ(0x55, sink.samples[0]);
    TEST_ASSERT_INT_EQ(0xd5, sink.samples[160]);
    TEST_ASSERT_INT_EQ(0x66, sink.samples[320]);
    TEST_ASSERT_INT_EQ(320, (int)buffer.byte_count);
    TEST_ASSERT_INT_EQ(480, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(160, (int)buffer.concealed_samples);
    TEST_ASSERT_INT_EQ(400, (int)buffer.last_timestamp_ms);
    TEST_ASSERT_INT_EQ(400, (int)buffer.snapshot_timestamp_ms);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 13, 7, 500));
    sink.failure = -5;
    TEST_ASSERT_INT_EQ(-5, df_runtime_audio_service(&jitter, &playout, 600));
    TEST_ASSERT_INT_EQ(2, (int)buffer.snapshot_packet_count);
    sink.failure = 0;
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 699));
    TEST_ASSERT_INT_EQ(2, sink.attempts);
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 700));
    TEST_ASSERT_INT_EQ(3, sink.attempts);
    TEST_ASSERT_INT_EQ(160, (int)sink.chunk.sample_count);
    TEST_ASSERT_INT_EQ(2, (int)sink.chunk.previous_revision);
    TEST_ASSERT_INT_EQ(3, (int)buffer.snapshot_packet_count);
}

/* Catches any admission relaxation or bypass of jitter by either transport's
 * shared payload receiver, including duplicates observed on both transports. */
void test_runtime_audio_receive_preserves_admission_and_reordering(void) {
    const uint8_t identity[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x61, 2, 2, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_PREVIEW, .generation = 7};
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_playout playout;
    struct df_gvs_audio_jitter jitter;
    struct runtime_audio_sink sink = {0};
    uint8_t samples[160], payload[DF_GVS_AUDIO_HEADER_LEN + 160];
    uint8_t captured[42 + sizeof(payload)] = {0};
    struct df_udp_prefix prefix;
    struct df_gvs_media_receiver receiver = {.audio_fd = -1, .video_fd = -1};
    struct df_gvs_media_datagram datagram;
    struct sockaddr_in destination = {0};
    uint8_t datagram_bytes[sizeof(payload)];
    size_t length = 0;
    memcpy(session.peer, peer, 6);
    memset(samples, 0x55, sizeof(samples));
    runtime_audio_init(&buffer, &playout, &jitter, &sink);
    df_gvs_audio_jitter_reset(&jitter, 7);
    df_gvs_audio_playout_reset(&playout, 7);
    TEST_ASSERT_INT_EQ(0, df_gvs_serialize_audio(identity, peer, 10,
        samples, sizeof(samples), payload, sizeof(payload), &length));
    /* Ethernet/IPv4/UDP capture path, with the original 8302 filter. */
    captured[12] = 8; captured[14] = 0x45; captured[23] = 17;
    captured[16] = (uint8_t)((28U + length) >> 8);
    captured[17] = (uint8_t)(28U + length);
    captured[34] = 0x20; captured[35] = 0x6e;
    captured[36] = 0x20; captured[37] = 0x6e;
    captured[38] = (uint8_t)((8U + length) >> 8);
    captured[39] = (uint8_t)(8U + length);
    memcpy(captured + 42, payload, length);
    TEST_ASSERT_INT_EQ(1, df_gvs_inspect_udp_prefix(captured,
        sizeof(captured), &prefix));
    TEST_ASSERT_INT_EQ(8302, prefix.destination_port);
    TEST_ASSERT_INT_EQ(1, prefix.payload_complete);
    TEST_ASSERT_INT_EQ(0, df_runtime_receive_audio_payload(
        captured + prefix.payload_offset, prefix.declared_payload_length,
        &session, identity, &jitter, 0));
    /* The bound socket path supplies the same complete GVS payload. */
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_receiver_open(&receiver,
        "127.0.0.1", 0, 0));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(receiver.audio_port);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_INT_EQ((int)length, (int)sendto(receiver.video_fd, payload,
        length, 0, (const struct sockaddr *)&destination, sizeof(destination)));
    int received = DF_GVS_MEDIA_RECEIVER_EMPTY;
    for (unsigned i = 0; i < 1000 && received == DF_GVS_MEDIA_RECEIVER_EMPTY; i++)
        received = df_gvs_media_receiver_next(&receiver, datagram_bytes,
            sizeof(datagram_bytes), &datagram);
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_RECEIVER_DATAGRAM, received);
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_AUDIO, datagram.channel);
    TEST_ASSERT_INT_EQ(1, df_runtime_receive_audio_payload(datagram_bytes,
        datagram.length, &session, identity, &jitter, 1));
    TEST_ASSERT_INT_EQ(0, df_gvs_serialize_audio(identity, peer, 12,
        samples, sizeof(samples), payload, sizeof(payload), &length));
    TEST_ASSERT_INT_EQ((int)length, (int)sendto(receiver.video_fd, payload,
        length, 0, (const struct sockaddr *)&destination, sizeof(destination)));
    received = DF_GVS_MEDIA_RECEIVER_EMPTY;
    for (unsigned i = 0; i < 1000 && received == DF_GVS_MEDIA_RECEIVER_EMPTY; i++)
        received = df_gvs_media_receiver_next(&receiver, datagram_bytes,
            sizeof(datagram_bytes), &datagram);
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_RECEIVER_DATAGRAM, received);
    TEST_ASSERT_INT_EQ(0, df_runtime_receive_audio_payload(datagram_bytes,
        datagram.length, &session, identity, &jitter, 20));
    TEST_ASSERT_INT_EQ(1, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_serialize_audio(identity, peer, 11,
        samples, sizeof(samples), payload, sizeof(payload), &length));
    memcpy(captured + 42, payload, length);
    TEST_ASSERT_INT_EQ(0, df_runtime_receive_audio_payload(
        captured + prefix.payload_offset, prefix.declared_payload_length,
        &session, identity, &jitter, 30));
    TEST_ASSERT_INT_EQ(3, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(1, (int)jitter.reordered_packets);
    payload[10] ^= 1;
    TEST_ASSERT_INT_EQ(1, df_runtime_receive_audio_payload(payload, length,
        &session, identity, &jitter, 31));
    payload[10] ^= 1; payload[16] ^= 1;
    TEST_ASSERT_INT_EQ(1, df_runtime_receive_audio_payload(payload, length,
        &session, identity, &jitter, 32));
    payload[16] ^= 1;
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(1, df_runtime_receive_audio_payload(payload, length,
        &session, identity, &jitter, 33));
    TEST_ASSERT_INT_EQ(3, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(1, (int)jitter.duplicate_packets);
    df_gvs_media_receiver_close(&receiver);
}

/* Catches inherited pre-call audio on TALKING, leaking queued tail on hangup
 * or preemption, and resetting video during a same-generation call pickup. */
void test_runtime_audio_lifecycle_clears_tail_and_preserves_call_video(void) {
    struct df_gvs_session session = {.state = DF_GVS_PREVIEW, .generation = 7};
    struct df_gvs_media_lifecycle lifecycle;
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_playout playout;
    struct df_gvs_audio_jitter jitter;
    struct df_gvs_video_reassembly video;
    struct df_gvs_video_frame_cache cache;
    struct runtime_audio_sink sink = {0};
    enum df_gvs_session_state previous_state = DF_GVS_IDLE;
    const uint8_t samples[160] = {0x55};
    const uint8_t jpeg[4] = {0xff, 0xd8, 0xff, 0xd9};
    char directory[] = "/tmp/doorfast-runtime-audio-XXXXXX";
    char audio_path[256], video_path[256], chunk_path[256], metadata_path[256];
    char parked_chunks[256];
    struct df_runtime_media_paths paths = {audio_path, chunk_path, video_path};
    FILE *file;
    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    (void)snprintf(audio_path, sizeof(audio_path), "%s/audio.wav", directory);
    (void)snprintf(video_path, sizeof(video_path), "%s/video.jpg", directory);
    (void)snprintf(chunk_path, sizeof(chunk_path), "%s/chunks", directory);
    (void)snprintf(parked_chunks, sizeof(parked_chunks), "%s/parked", directory);
    (void)snprintf(metadata_path, sizeof(metadata_path),
        "%s/chunks/chunk-7-0-1-364-0-20-0.wav", directory);
    runtime_audio_init(&buffer, &playout, &jitter, &sink);
    df_gvs_video_reassembly_init(&video);
    df_gvs_video_frame_cache_init(&cache);
    df_gvs_media_lifecycle_init(&lifecycle);
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 10, 7, 0));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 12, 7, 20));
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_store(&cache, jpeg,
        sizeof(jpeg), 7, 1, 20));
    TEST_ASSERT_INT_EQ(0, df_g711_alaw_write_wav(audio_path, samples,
        sizeof(samples)));
    file = fopen(video_path, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) { (void)fwrite(jpeg, 1, sizeof(jpeg), file); (void)fclose(file); }
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(chunk_path,
        audio_path, 7, 0, 1, 364, 0, 20, false));
    TEST_ASSERT_INT_EQ(0, access(metadata_path, F_OK));
    session.state = DF_GVS_TALKING;
    /* A storage fault must close audio until cleanup is actually successful. */
    TEST_ASSERT_INT_EQ(0, rename(chunk_path, parked_chunks));
    file = fopen(chunk_path, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) (void)fclose(file);
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(0, (int)jitter.generation);
    TEST_ASSERT_INT_EQ(0, (int)playout.generation);
    TEST_ASSERT_INT_EQ(0, (int)buffer.generation);
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 1, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 200));
    TEST_ASSERT_INT_EQ(0, sink.attempts);
    TEST_ASSERT_INT_EQ(4, (int)cache.length);
    TEST_ASSERT_INT_EQ(0, remove(chunk_path));
    TEST_ASSERT_INT_EQ(0, rename(parked_chunks, chunk_path));
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(7, (int)playout.generation);
    TEST_ASSERT_INT_EQ(0, (int)buffer.length);
    TEST_ASSERT_INT_EQ(0, jitter.has_sequence);
    TEST_ASSERT_INT_EQ(0, playout.has_pending);
    TEST_ASSERT_INT_EQ(4, (int)cache.length);
    TEST_ASSERT_INT_EQ(-1, access(audio_path, F_OK));
    TEST_ASSERT_INT_EQ(-1, access(metadata_path, F_OK));
    TEST_ASSERT_INT_EQ(0, access(video_path, F_OK));
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 200));
    TEST_ASSERT_INT_EQ(0, sink.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 1, 7, 300));
    session.generation = 8;
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(8, (int)jitter.generation);
    TEST_ASSERT_INT_EQ(0, (int)cache.length);
    TEST_ASSERT_INT_EQ(-1, access(video_path, F_OK));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 2, 7, 310));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 1, 8, 320));
    /* A caller preemption increments generation while still RINGING. */
    session.state = DF_GVS_RINGING; session.generation = 9;
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(9, (int)playout.generation);
    TEST_ASSERT_INT_EQ(0, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 1, 9, 400));
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(9, (int)playout.generation);
    TEST_ASSERT_INT_EQ(0, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 1, 9, 405));
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(0, df_runtime_media_sync(&lifecycle, &session, &video,
        &cache, &jitter, &playout, &previous_state, &paths));
    TEST_ASSERT_INT_EQ(0, (int)jitter.generation);
    TEST_ASSERT_INT_EQ(0, (int)playout.generation);
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples,
        sizeof(samples), 2, 9, 410));
    TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 600));
    TEST_ASSERT_INT_EQ(0, sink.attempts);
    (void)df_gvs_audio_chunk_store_clear(chunk_path);
    (void)rmdir(chunk_path);
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}

struct delay_trace {
    unsigned values[20];
    size_t count;
    bool fail_second;
};

static int runtime_service_status(
    struct df_gvs_runtime_sync_status *status, void *context) {
    (void)context;
    if (status == NULL) return DF_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    status->phase = DF_GVS_PRESENCE_PERIODIC;
    status->role = DF_GVS_SYNC_ROLE_FOLLOWER;
    return DF_OK;
}

struct auto_unlock_trace {
    unsigned calls;
    uint8_t destination[6];
};

struct auto_hangup_trace {
    unsigned calls;
    struct df_runtime_call_request request;
};

static int auto_hangup_status(struct df_gvs_call_control_status *status,
    void *context) {
    (void)context;
    if (status == NULL) return DF_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    status->session_state = DF_GVS_RINGING;
    return DF_OK;
}

static int record_auto_hangup(const struct df_runtime_call_request *request,
    uint64_t now_ms, void *context) {
    struct auto_hangup_trace *trace = context;
    (void)now_ms;
    if (request == NULL || trace == NULL) return DF_ERR_INVALID;
    trace->request = *request;
    trace->calls++;
    return DF_OK;
}

static int record_auto_unlock(
    const struct df_gvs_access_request *request, void *context) {
    struct auto_unlock_trace *trace = context;
    if (request == NULL || trace == NULL) return DF_ERR_INVALID;
    memcpy(trace->destination, request->destination,
        sizeof(trace->destination));
    trace->calls++;
    return DF_OK;
}

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

void test_runtime_auto_unlock_uses_calling_station_once(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station_address[6] = {0x32, 2, 1, 0, 2, 0};
    struct df_station station = {
        .id = "gate_side", .logical_address = {0x32, 2, 1, 0, 2, 0},
        .enabled = true,
    };
    const struct df_station_registry stations = {
        .items = &station, .count = 1U,
    };
    struct df_runtime_ubus ubus = {0};
    struct df_gvs_access_control access;
    struct df_gvs_session access_session = {0};
    struct auto_unlock_trace unlock = {0};
    struct auto_hangup_trace hangup = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_runtime_media_module media = {0};
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE];
    size_t packet_length = 0U;
    int media_result = DF_ERR_INVALID;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &access, "0011223344556677", 0, record_auto_unlock, &unlock));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(
        &ubus, runtime_service_status, NULL, 10U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_call(
        &ubus, auto_hangup_status, record_auto_hangup, &hangup));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(
        &ubus, &access, &session, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(
        &ubus, &access_session, &stations));
    df_runtime_ubus_set_active_host(&ubus, true);
    df_runtime_ubus_set_auto_unlock(&ubus, true);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, local, station_address, 0x03U, 0x01U, NULL, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(
        &media, &ubus, NULL, &stations, &control, packet, packet_length,
        local, &session, &deadline, htonl(0x7f000001U), 100U, &result,
        &media_result));
    TEST_ASSERT_INT_EQ(0, (int)unlock.calls);
    TEST_ASSERT_INT_EQ(1, ubus.auto_unlock_pending);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(
        &media, &ubus, NULL, &stations, &control, packet, packet_length,
        local, &session, &deadline, htonl(0x7f000001U), 101U, &result,
        &media_result));
    TEST_ASSERT_INT_EQ(0, (int)unlock.calls);
    {
        struct df_media_session_status_v3 entry = {
            .station_id = "gate_side", .generation = 20U,
            .purpose = DF_MEDIA_SESSION_CALL, .active = true,
        };
        struct df_runtime_media_status status = {
            .sessions = &entry, .session_count = 1U,
        };
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_tick_auto_unlock(&ubus, &status, 102U));
        TEST_ASSERT_INT_EQ(0, (int)unlock.calls);
        entry.ready = true;
        /* A manually sent request remains serialized even when its media
         * generation has been replaced by an incoming call. */
        access_session.state = DF_GVS_PREVIEW;
        access_session.generation = 5U;
        memcpy(access_session.peer, station_address, 6U);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_submit(&access,
            &access_session, 5U, local, 102U));
        TEST_ASSERT_INT_EQ(1, (int)unlock.calls);
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_tick_auto_unlock(&ubus, &status, 103U));
        TEST_ASSERT_INT_EQ(1, (int)unlock.calls);
        TEST_ASSERT_INT_EQ(1, ubus.auto_unlock_pending);
        TEST_ASSERT_INT_EQ(0, ubus.auto_unlock_inflight);
        access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_handle_auto_unlock_result(&ubus, 103U));
        TEST_ASSERT_INT_EQ(0, (int)hangup.calls);
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_tick_auto_unlock(&ubus, &status, 104U));
        TEST_ASSERT_INT_EQ(2, (int)unlock.calls);
        TEST_ASSERT_INT_EQ(0, memcmp(station_address, unlock.destination, 6));
        access.result.state = DF_GVS_ACCESS_PROTOCOL_COMPLETED;
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_handle_auto_unlock_result(&ubus, 105U));
        TEST_ASSERT_INT_EQ(1, (int)hangup.calls);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_HANGUP,
            hangup.request.type);
        TEST_ASSERT_INT_EQ(1, (int)hangup.request.session_generation);
        TEST_ASSERT_INT_EQ(DF_OK,
            df_runtime_ubus_tick_auto_unlock(&ubus, &status, 106U));
        TEST_ASSERT_INT_EQ(2, (int)unlock.calls);
    }
    df_runtime_ubus_stop(&ubus);
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

void test_runtime_media_forwards_station_preview_end_control(void) {
    const uint8_t local[6] = {0x61U, 2U, 1U, 1U, 1U, 1U};
    const uint8_t station[6] = {0x32U, 2U, 1U, 0U, 2U, 0U};
    const uint8_t preview_end[] = {0x01U};
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
    uint8_t packet[DF_GVS_CONTROL_HEADER_SIZE + sizeof(preview_end)];
    size_t packet_length = 0U;
    int media_result = DF_ERR_INVALID;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0U,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(packet, sizeof(packet),
        &packet_length, local, station, 0x03U, 0x02U, preview_end,
        sizeof(preview_end), df_gvs_placeholder_header_fields, NULL));
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

    trace.failed_generation = 13U;
    trace.failed_error = DF_MEDIA_ERROR_VIDEO_STALLED;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_tick_with_event(&media, &ubus, NULL, 902U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 1U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=media_pipeline_failed station_id=gate_side generation=13 "
        "error=video_stalled", log_entry.message));

    trace.failed_generation = 14U;
    trace.failed_error = DF_MEDIA_ERROR_RECONNECT_FRAME;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_media_tick_with_event(&media, &ubus, NULL, 903U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_log_get(&ubus, 2U, &log_entry));
    TEST_ASSERT_INT_EQ(0, strcmp(
        "event=media_pipeline_failed station_id=gate_side generation=14 "
        "error=reconnect_frame_failed", log_entry.message));
}
