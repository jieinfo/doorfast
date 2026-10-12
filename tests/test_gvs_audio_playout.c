#include "test.h"
#include "gvs_audio_playout.h"
#include <string.h>

struct publication {
    unsigned int attempts;
    int failure;
    struct df_gvs_audio_chunk chunk;
    uint8_t samples[1600];
};

/* A synchronous sink observes real exported bytes and metadata; failure models
 * the external publisher without replacing buffer or playout behavior. */
static int publish(const uint8_t *samples, const struct df_gvs_audio_chunk *chunk,
                   void *context) {
    struct publication *publication = context;
    publication->attempts++;
    publication->chunk = *chunk;
    if (chunk->sample_count > sizeof(publication->samples)) return -1;
    memcpy(publication->samples, samples, chunk->sample_count);
    return publication->failure;
}

static int push_real(struct df_gvs_audio_playout *p, size_t count,
                     uint16_t sequence, uint64_t generation, uint64_t time,
                     uint8_t value, bool discontinuity) {
    uint8_t samples[256];
    struct df_gvs_audio_emission emission = {
        .samples = samples, .count = count, .sequence = sequence,
        .generation = generation, .timestamp_ms = time,
        .discontinuity = discontinuity,
    };
    if (count > sizeof(samples)) return -1;
    memset(samples, value, count);
    return df_gvs_audio_playout_push(p, &emission);
}

/* Catches publishing a short first chunk before its 100 ms deadline, and
 * missing the 800-sample threshold or carrying the old publication timer. */
void test_gvs_audio_playout_deadlines(void) {
    struct df_gvs_audio_buffer b;
    struct df_gvs_audio_playout p;
    struct publication publication = {0};
    unsigned int i;
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(&p, &b, publish, &publication));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 0));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 10, 7, 0, 0x11, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 99));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(128, (int)publication.chunk.sample_count);
    TEST_ASSERT_INT_EQ(7, (int)publication.chunk.generation);
    TEST_ASSERT_INT_EQ(0, (int)publication.chunk.previous_revision);
    TEST_ASSERT_INT_EQ(1, (int)publication.chunk.revision);
    TEST_ASSERT_INT_EQ(100, (int)publication.chunk.timestamp_ms);
    TEST_ASSERT_INT_EQ(0x11, publication.samples[127]);
    TEST_ASSERT_INT_EQ(0, (int)publication.chunk.dropped_samples);
    TEST_ASSERT_INT_EQ(0, publication.chunk.discontinuity);
    TEST_ASSERT_INT_EQ(1, (int)b.snapshot_packet_count);
    for (i = 0; i < 5; i++)
        TEST_ASSERT_INT_EQ(0, push_real(&p, 160, (uint16_t)(11 + i), 7,
            200 + i * 20, (uint8_t)(0x20 + i), false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 280));
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    TEST_ASSERT_INT_EQ(800, (int)publication.chunk.sample_count);
    TEST_ASSERT_INT_EQ(1, (int)publication.chunk.previous_revision);
    TEST_ASSERT_INT_EQ(6, (int)publication.chunk.revision);
    TEST_ASSERT_INT_EQ(0x20, publication.samples[0]);
    TEST_ASSERT_INT_EQ(0x24, publication.samples[799]);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 16, 7, 400, 0x33, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 499));
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 500));
    TEST_ASSERT_INT_EQ(3, publication.attempts);
}

/* Catches exporting stale backlog instead of the latest bounded 1600 samples,
 * losing discontinuity or consuming pending samples on a failed callback. */
void test_gvs_audio_playout_tail_and_retry(void) {
    struct df_gvs_audio_buffer b;
    struct df_gvs_audio_playout p;
    struct publication publication = {0};
    unsigned int i;
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(&p, &b, publish, &publication));
    df_gvs_audio_playout_reset(&p, 7);
    for (i = 0; i < 20; i++)
        TEST_ASSERT_INT_EQ(0, push_real(&p, 160, (uint16_t)i, 7,
            i * 10, (uint8_t)i, i == 3));
    publication.failure = -5;
    TEST_ASSERT_INT_EQ(-5, df_gvs_audio_playout_tick(&p, 200));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(1600, (int)publication.chunk.sample_count);
    TEST_ASSERT_INT_EQ(1600, (int)publication.chunk.dropped_samples);
    TEST_ASSERT_INT_EQ(10, publication.samples[0]);
    TEST_ASSERT_INT_EQ(19, publication.samples[1599]);
    TEST_ASSERT_INT_EQ(1, publication.chunk.discontinuity);
    TEST_ASSERT_INT_EQ(0, (int)b.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(0, (int)b.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(0, b.snapshot_discontinuity);
    publication.failure = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 210));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 299));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 300));
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    TEST_ASSERT_INT_EQ(20, (int)b.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(3200, (int)b.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(1600, (int)b.snapshot_dropped_bytes);
    TEST_ASSERT_INT_EQ(3244, (int)b.snapshot_bytes);
    TEST_ASSERT_INT_EQ(1, b.snapshot_discontinuity);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 400));
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 20, 7, 500, 0x55, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 600));
    TEST_ASSERT_INT_EQ(0, publication.chunk.discontinuity);
    TEST_ASSERT_INT_EQ(0, (int)publication.chunk.dropped_samples);
    TEST_ASSERT_INT_EQ(0, b.snapshot_discontinuity);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 21, 7, 700, 0x55, true));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 800));
    TEST_ASSERT_INT_EQ(1, b.snapshot_discontinuity);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 22, 7, 900, 0x55, false));
    publication.failure = -5;
    TEST_ASSERT_INT_EQ(-5, df_gvs_audio_playout_tick(&p, 1000));
    TEST_ASSERT_INT_EQ(1, b.snapshot_discontinuity);
    df_gvs_audio_playout_reset(&p, 8);
    TEST_ASSERT_INT_EQ(0, b.snapshot_discontinuity);
}

/* Catches silence inventing a real revision, reset retaining failure deadlines,
 * an emission from another generation reviving a closed or bound session. */
void test_gvs_audio_playout_silence_and_reset(void) {
    struct df_gvs_audio_buffer b;
    struct df_gvs_audio_playout p;
    struct publication publication = {0};
    struct df_gvs_audio_emission silence = {
        .count = 160, .generation = 7, .timestamp_ms = 0,
        .synthetic = true, .discontinuity = true,
    };
    unsigned int i;
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(&p, &b, publish, &publication));
    df_gvs_audio_playout_reset(&p, 7);
    for (i = 0; i < 5; i++)
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_push(&p, &silence));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    TEST_ASSERT_INT_EQ(0, (int)b.packet_count);
    TEST_ASSERT_INT_EQ(0, (int)b.byte_count);
    TEST_ASSERT_INT_EQ(800, (int)b.sample_count);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 10, 7, 100, 0x66, false));
    publication.failure = -1;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(928, (int)publication.chunk.sample_count);
    TEST_ASSERT_INT_EQ(0xd5, publication.samples[799]);
    TEST_ASSERT_INT_EQ(0x66, publication.samples[927]);
    TEST_ASSERT_INT_EQ(1, publication.chunk.discontinuity);
    df_gvs_audio_playout_reset(&p, 8);
    publication.failure = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 50));
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 0, 8, 50, 0x77, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 149));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 150));
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    TEST_ASSERT_INT_EQ(8, (int)publication.chunk.generation);
    TEST_ASSERT_INT_EQ(0, (int)publication.chunk.previous_revision);
    TEST_ASSERT_INT_EQ(1, (int)publication.chunk.revision);
    TEST_ASSERT_INT_EQ(0, publication.chunk.discontinuity);
    TEST_ASSERT_INT_EQ(1, push_real(&p, 128, 1, 9, 160, 0x88, false));
    df_gvs_audio_playout_reset(&p, 0);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 1000));
    TEST_ASSERT_INT_EQ(1, push_real(&p, 128, 1, 9, 1000, 0x88, false));
    TEST_ASSERT_INT_EQ(0, (int)b.length);
    TEST_ASSERT_INT_EQ(2, publication.attempts);
    df_gvs_audio_playout_reset(&p, 9);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 1, 9, 1000, 0x88, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, 1100));
    TEST_ASSERT_INT_EQ(3, publication.attempts);
}

/* Catches a publisher side effect occurring before predictable commit/clock
 * errors have been ruled out, and overflow in 100 ms scheduling. */
void test_gvs_audio_playout_preflight_limits(void) {
    struct df_gvs_audio_buffer b;
    struct df_gvs_audio_playout p;
    struct publication publication = {0};
    unsigned int i;
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_init(&p, &b, publish, &publication));
    df_gvs_audio_playout_reset(&p, 7);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 1, 7, 0, 0x11, false));
    b.snapshot_source_bytes = 129;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    b.snapshot_source_bytes = 0;
    b.snapshot_source_samples = 129;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    b.snapshot_source_samples = 0;
    b.generation = 8;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, 100));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    df_gvs_audio_playout_reset(&p, 7);
    for (i = 0; i < 5; i++)
        TEST_ASSERT_INT_EQ(0, push_real(&p, 160, (uint16_t)i, 7,
            UINT64_MAX - 100, 0x33, false));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, UINT64_MAX - 99));
    TEST_ASSERT_INT_EQ(0, publication.attempts);
    publication.failure = -1;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, UINT64_MAX - 100));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, UINT64_MAX - 1));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_playout_tick(&p, UINT64_MAX));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
    df_gvs_audio_playout_reset(&p, 8);
    TEST_ASSERT_INT_EQ(0, push_real(&p, 128, 1, 8, UINT64_MAX - 50, 0x44, false));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&p, UINT64_MAX));
    TEST_ASSERT_INT_EQ(1, publication.attempts);
}
