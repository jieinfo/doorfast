#include "test.h"
#include "g711_alaw.h"
#include "gvs_audio_playout.h"
#include "gvs_media.h"

#include <string.h>

struct expected_span {
    size_t count;
    uint8_t alaw;
    int16_t pcm;
};

/* The sink retains only counters, metadata and a PCM checksum, never audio.
 * Expected spans are synthetic fixtures, independent of the real decoder. */
struct publications {
    unsigned count, attempts;
    struct df_gvs_audio_chunk last;
    uint32_t checksum;
    uint64_t expected_ms;
    size_t total_samples;
    int failure;
    const struct expected_span *expected;
    size_t span_count;
};

static uint32_t checksum_sample(uint32_t checksum, int16_t sample) {
    uint16_t bits = (uint16_t)sample;
    checksum = (checksum ^ (uint8_t)bits) * UINT32_C(16777619);
    return (checksum ^ (uint8_t)(bits >> 8)) * UINT32_C(16777619);
}

static int publish(const uint8_t *samples, const struct df_gvs_audio_chunk *chunk,
                   void *context) {
    struct publications *published = context;
    int16_t pcm[1600];
    uint32_t actual = UINT32_C(2166136261), expected = actual;
    size_t offset = 0;
    published->attempts++;
    published->last = *chunk;
    TEST_ASSERT_INT_EQ(1, chunk->timestamp_ms == published->expected_ms);
    TEST_ASSERT_INT_EQ(1, chunk->sample_count <= 1600U);
    if (chunk->sample_count > 1600U) return -1;
    TEST_ASSERT_INT_EQ(0, df_g711_alaw_decode(samples, chunk->sample_count,
                                            pcm, 1600));
    for (size_t i = 0; i < published->span_count; i++) {
        const struct expected_span *span = &published->expected[i];
        for (size_t j = 0; j < span->count; j++, offset++) {
            if (offset >= chunk->sample_count) {
                TEST_ASSERT_INT_EQ(1, offset < chunk->sample_count);
                return -1;
            }
            TEST_ASSERT_INT_EQ(span->alaw, samples[offset]);
            TEST_ASSERT_INT_EQ(span->pcm, pcm[offset]);
            expected = checksum_sample(expected, span->pcm);
            actual = checksum_sample(actual, pcm[offset]);
        }
    }
    TEST_ASSERT_INT_EQ(1, offset == chunk->sample_count);
    TEST_ASSERT_INT_EQ(1, actual == expected);
    published->checksum = actual;
    if (published->failure != 0) return published->failure;
    published->count++;
    published->total_samples += chunk->sample_count;
    return 0;
}

static int emit(const struct df_gvs_audio_emission *emission, void *context) {
    return df_gvs_audio_playout_push(context, emission);
}

static int receive(struct df_gvs_audio_jitter *jitter, size_t count,
                   uint8_t value, uint16_t sequence, uint64_t generation,
                   uint64_t now_ms) {
    const uint8_t destination[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t source[6] = {6, 5, 4, 3, 2, 1};
    uint8_t samples[256], wire[DF_GVS_AUDIO_HEADER_LEN + 256];
    struct df_gvs_audio_packet packet;
    size_t length = 0;
    if (count > sizeof(samples)) return -1;
    memset(samples, value, count);
    if (df_gvs_serialize_audio(destination, source, sequence, samples, count,
                               wire, sizeof(wire), &length) != 0) return -1;
    TEST_ASSERT_INT_EQ((int)(DF_GVS_AUDIO_HEADER_LEN + count), (int)length);
    if (df_gvs_parse_audio(wire, length, &packet) != 0) return -1;
    TEST_ASSERT_INT_EQ(0, memcmp(destination, packet.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(source, packet.source, 6));
    TEST_ASSERT_INT_EQ(sequence, packet.sequence);
    TEST_ASSERT_INT_EQ((int)count, (int)packet.payload_length);
    TEST_ASSERT_INT_EQ((int)count, (int)packet.field_c);
    TEST_ASSERT_INT_EQ(1, packet.field_d);
    TEST_ASSERT_INT_EQ(1, packet.field_e);
    TEST_ASSERT_INT_EQ(0x100, packet.field_f);
    TEST_ASSERT_INT_EQ(0, memcmp(samples, packet.payload, count));
    return df_gvs_audio_jitter_push(jitter, packet.payload, packet.payload_length,
                                   packet.sequence, generation, now_ms);
}

static void start(struct df_gvs_audio_buffer *buffer,
                  struct df_gvs_audio_jitter *jitter,
                  struct df_gvs_audio_playout *playout,
                  struct publications *published) {
    df_gvs_audio_buffer_init(buffer);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(playout, buffer, publish,
                                                 published));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_init(jitter, emit, playout));
}

/* Real wire round trips, wraparound/reorder/gap concealment and generation
 * changes must preserve exact PCM order without inventing packet revisions. */
void test_audio_receive_pipeline_sequence_and_reset(void) {
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_jitter jitter;
    struct df_gvs_audio_playout playout;
    struct publications published = {0};
    const struct expected_span first[] = {
        {128, 0xd5, 8}, {160, 0x55, -8}, {192, 0x00, -5504},
        {192, 0xd5, 8}, {128, 0x80, 5504},
    };
    const struct expected_span second[] = {
        {192, 0x80, 5504}, {160, 0x00, -5504}, {128, 0x55, -8},
    };
    uint8_t stale_samples[160];
    struct df_gvs_audio_emission stale = {
        .samples = stale_samples, .count = 160, .sequence = 4,
        .generation = 7, .timestamp_ms = 400,
    };
    memset(stale_samples, 0xd5, sizeof(stale_samples));
    start(&buffer, &jitter, &playout, &published);
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 128, 0xd5, 65534, 7, 100));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 192, 0x00, 0, 7, 110));
    TEST_ASSERT_INT_EQ(1, receive(&jitter, 192, 0x00, 0, 7, 111));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 160, 0x55, 65535, 7, 115));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 128, 0x80, 2, 7, 120));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 159));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 159));
    TEST_ASSERT_INT_EQ(0, published.attempts);
    TEST_ASSERT_INT_EQ(3, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(480, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(1, receive(&jitter, 160, 0x55, 1, 7, 161));
    TEST_ASSERT_INT_EQ(1, (int)jitter.duplicate_packets);
    TEST_ASSERT_INT_EQ(1, (int)jitter.late_packets);
    TEST_ASSERT_INT_EQ(2, (int)jitter.reordered_packets);
    TEST_ASSERT_INT_EQ(4, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(608, (int)buffer.byte_count);
    TEST_ASSERT_INT_EQ(800, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(192, (int)buffer.concealed_samples);
    TEST_ASSERT_INT_EQ(1, (int)buffer.sequence_gaps);
    TEST_ASSERT_INT_EQ(1, (int)buffer.missing_packets);
    published.expected = first;
    published.span_count = sizeof(first) / sizeof(first[0]);
    published.expected_ms = 200;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 200));
    TEST_ASSERT_INT_EQ(1, published.count);
    TEST_ASSERT_INT_EQ(7, (int)published.last.generation);
    TEST_ASSERT_INT_EQ(800, (int)published.last.sample_count);
    TEST_ASSERT_INT_EQ(0, (int)published.last.previous_revision);
    TEST_ASSERT_INT_EQ(4, (int)published.last.revision);
    TEST_ASSERT_INT_EQ(0, (int)published.last.dropped_samples);
    TEST_ASSERT_INT_EQ(0, published.last.discontinuity);
    TEST_ASSERT_INT_EQ(800, (int)buffer.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(608, (int)buffer.snapshot_source_bytes);

    /* Leave both already emitted pending samples and an unresolved jitter slot. */
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 160, 0xd5, 3, 7, 210));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 192, 0xd5, 5, 7, 220));
    df_gvs_audio_jitter_reset(&jitter, 8);
    df_gvs_audio_playout_reset(&playout, 8);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 400));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 400));
    TEST_ASSERT_INT_EQ(1, published.count);
    TEST_ASSERT_INT_EQ(1, published.attempts);
    TEST_ASSERT_INT_EQ(0, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(1, receive(&jitter, 128, 0xd5, 6, 7, 400));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_playout_push(&playout, &stale));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 192, 0x80, 65535, 8, 410));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 160, 0x00, 0, 8, 430));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 128, 0x55, 1, 8, 450));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 509));
    TEST_ASSERT_INT_EQ(1, published.count);
    published.expected = second;
    published.span_count = sizeof(second) / sizeof(second[0]);
    published.expected_ms = 510;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 510));
    TEST_ASSERT_INT_EQ(2, published.count);
    TEST_ASSERT_INT_EQ(8, (int)published.last.generation);
    TEST_ASSERT_INT_EQ(0, (int)published.last.previous_revision);
    TEST_ASSERT_INT_EQ(3, (int)published.last.revision);
    TEST_ASSERT_INT_EQ(480, (int)published.last.sample_count);
    TEST_ASSERT_INT_EQ(1280, (int)published.total_samples);
    TEST_ASSERT_INT_EQ(0, (int)buffer.concealed_samples);
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 128, 0xd5, 2, 8, 520));
    df_gvs_audio_jitter_reset(&jitter, 0);
    df_gvs_audio_playout_reset(&playout, 0);
    TEST_ASSERT_INT_EQ(1, receive(&jitter, 160, 0xd5, 3, 8, 530));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_playout_push(&playout, &stale));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 700));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 700));
    TEST_ASSERT_INT_EQ(2, published.count);
    TEST_ASSERT_INT_EQ(2, published.attempts);
}

/* Verify bounded tail selection and real decode on a failing sink and retry;
 * pending real counters commit only after a successful publication. */
void test_audio_receive_pipeline_tail_and_retry(void) {
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_jitter jitter;
    struct df_gvs_audio_playout playout;
    struct publications published = {0};
    const struct expected_span tail[] = {
        {640, 0x55, -8}, {960, 0x80, 5504},
    };
    start(&buffer, &jitter, &playout, &published);
    for (unsigned i = 0; i < 20; i++)
        TEST_ASSERT_INT_EQ(0, receive(&jitter, 160,
            i < 10 ? 0x00 : (i < 14 ? 0x55 : 0x80),
            (uint16_t)i, 7, 10U * i));
    published.expected = tail;
    published.span_count = sizeof(tail) / sizeof(tail[0]);
    published.expected_ms = 200;
    published.failure = -5;
    TEST_ASSERT_INT_EQ(-5, df_gvs_audio_playout_tick(&playout, 200));
    uint32_t failed_checksum = published.checksum;
    TEST_ASSERT_INT_EQ(1, published.attempts);
    TEST_ASSERT_INT_EQ(0, published.count);
    TEST_ASSERT_INT_EQ(20, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(3200, (int)buffer.byte_count);
    TEST_ASSERT_INT_EQ(3200, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(1600, (int)published.last.sample_count);
    TEST_ASSERT_INT_EQ(1600, (int)published.last.dropped_samples);
    TEST_ASSERT_INT_EQ(1, published.last.discontinuity);
    TEST_ASSERT_INT_EQ(0, (int)buffer.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(0, (int)buffer.snapshot_packet_count);
    published.failure = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 299));
    TEST_ASSERT_INT_EQ(1, published.attempts);
    published.expected_ms = 300;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 300));
    TEST_ASSERT_INT_EQ(2, published.attempts);
    TEST_ASSERT_INT_EQ(1, published.count);
    TEST_ASSERT_INT_EQ(1, failed_checksum == published.checksum);
    TEST_ASSERT_INT_EQ(0, (int)published.last.previous_revision);
    TEST_ASSERT_INT_EQ(20, (int)published.last.revision);
    TEST_ASSERT_INT_EQ(3200, (int)buffer.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(1600, (int)buffer.snapshot_dropped_bytes);
    TEST_ASSERT_INT_EQ(3244, (int)buffer.snapshot_bytes);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 400));
    TEST_ASSERT_INT_EQ(2, published.attempts);
}

/* A jitter-admitted 32767-gap successor must survive the downstream buffer's
 * half-range check after bounded concealment. The expected payload and
 * metadata are checked independently of the transport fixtures. */
void test_audio_receive_pipeline_large_gap_successor(void) {
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_jitter jitter;
    struct df_gvs_audio_playout playout;
    struct df_gvs_audio_buffer direct;
    struct publications published = {0};
    const struct expected_span expected[] = {
        {160, 0x55, -8}, {320, 0xd5, 8}, {160, 0x80, 5504},
    };

    start(&buffer, &jitter, &playout, &published);
    published.expected = expected;
    published.span_count = sizeof(expected) / sizeof(expected[0]);
    published.expected_ms = 200;
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 160, 0x55, 65535, 7, 100));
    TEST_ASSERT_INT_EQ(0, receive(&jitter, 160, 0x80, 32767, 7, 120));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 159));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 159));
    TEST_ASSERT_INT_EQ(0, published.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 199));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 200));

    TEST_ASSERT_INT_EQ(1, published.count);
    TEST_ASSERT_INT_EQ(1, published.attempts);
    TEST_ASSERT_INT_EQ(7, (int)published.last.generation);
    TEST_ASSERT_INT_EQ(0, (int)published.last.previous_revision);
    TEST_ASSERT_INT_EQ(2, (int)published.last.revision);
    TEST_ASSERT_INT_EQ(200, (int)published.last.timestamp_ms);
    TEST_ASSERT_INT_EQ(640, (int)published.last.sample_count);
    TEST_ASSERT_INT_EQ(0, (int)published.last.dropped_samples);
    TEST_ASSERT_INT_EQ(1, published.last.discontinuity);
    TEST_ASSERT_INT_EQ(2, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(320, (int)buffer.byte_count);
    TEST_ASSERT_INT_EQ(640, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(320, (int)buffer.concealed_samples);
    TEST_ASSERT_INT_EQ(1, (int)buffer.sequence_gaps);
    TEST_ASSERT_INT_EQ(32767, (int)buffer.missing_packets);
    TEST_ASSERT_INT_EQ(640, (int)buffer.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(320, (int)buffer.snapshot_source_bytes);
    TEST_ASSERT_INT_EQ(1, (int)jitter.reordered_packets);
    TEST_ASSERT_INT_EQ(0, (int)jitter.late_packets);

    /* The legacy direct-buffer path still rejects the same half-range-old
     * sequence; only the composed jitter successor uses the narrow bypass. */
    df_gvs_audio_buffer_init(&direct);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &direct, (const uint8_t *)"x", 1, 65535, 7, 0));
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_LATE,
        df_gvs_audio_buffer_push(&direct, (const uint8_t *)"y", 1,
                                 32767, 7, 1));
}
