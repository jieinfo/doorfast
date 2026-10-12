#include "test.h"
#include "gvs_audio_jitter.h"

#include <string.h>

struct trace {
    int real_count;
    int synthetic_samples;
    uint16_t sequences[16];
    size_t real_samples[16];
    uint64_t timestamps[16];
    uint64_t generations[16];
    bool discontinuities[16];
    int synthetic_count;
    uint64_t synthetic_timestamp;
    uint16_t synthetic_sequence;
    bool synthetic_discontinuity;
    int fail_real;
    int fail_synthetic;
    bool fail_specific_real;
    uint16_t fail_sequence;
    int failure;
};

static int record_emission(const struct df_gvs_audio_emission *emission,
                           void *context)
{
    struct trace *trace = context;
    if ((emission->synthetic && trace->fail_synthetic) ||
        (!emission->synthetic && (trace->fail_real ||
         (trace->fail_specific_real && emission->sequence == trace->fail_sequence))))
        return trace->failure;
    if (emission->synthetic) {
        for (size_t i = 0; i < emission->count; i++)
            TEST_ASSERT_INT_EQ(0xd5, emission->samples[i]);
        trace->synthetic_samples += (int)emission->count;
        trace->synthetic_count++;
        trace->synthetic_timestamp = emission->timestamp_ms;
        trace->synthetic_sequence = emission->sequence;
        trace->synthetic_discontinuity = emission->discontinuity;
    } else {
        int index = trace->real_count;
        if (index >= 16) return -1;
        for (size_t i = 0; i < emission->count; i++)
            TEST_ASSERT_INT_EQ(0x23, emission->samples[i]);
        trace->sequences[index] = emission->sequence;
        trace->real_samples[index] = emission->count;
        trace->timestamps[index] = emission->timestamp_ms;
        trace->generations[index] = emission->generation;
        trace->discontinuities[index] = emission->discontinuity;
        trace->real_count++;
    }
    return 0;
}

static void start(struct df_gvs_audio_jitter *jitter, struct trace *trace)
{
    memset(trace, 0, sizeof(*trace));
    trace->failure = -9;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_init(jitter, record_emission, trace));
    df_gvs_audio_jitter_reset(jitter, 7);
}

void test_gvs_audio_jitter_reorders_and_bounds_gaps(void)
{
    struct df_gvs_audio_jitter jitter;
    struct trace trace;
    uint8_t samples[256];
    memset(samples, 0x23, sizeof(samples));

    /* Immediate emission or discarding seq 11 breaks the reorder contract. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 128, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 192, 12, 7, 120));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 135));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(10, trace.sequences[0]);
    TEST_ASSERT_INT_EQ(11, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(12, trace.sequences[2]);
    TEST_ASSERT_INT_EQ(128, (int)trace.real_samples[0]);
    TEST_ASSERT_INT_EQ(160, (int)trace.real_samples[1]);
    TEST_ASSERT_INT_EQ(192, (int)trace.real_samples[2]);
    TEST_ASSERT_INT_EQ(135, (int)trace.timestamps[2]);
    TEST_ASSERT_INT_EQ(7, (int)trace.generations[2]);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, (int)jitter.reordered_packets);

    /* Starting the deadline from output/last arrival shifts the 160 ms bound. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 120));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 159));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.synthetic_count);
    TEST_ASSERT_INT_EQ(11, trace.synthetic_sequence);
    TEST_ASSERT_INT_EQ(160, (int)trace.synthetic_timestamp);
    TEST_ASSERT_INT_EQ(160, (int)trace.timestamps[1]);
    TEST_ASSERT_INT_EQ(0, trace.discontinuities[1]);

    /* Size estimates use the previous real packet, capped at 320 samples. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 128, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 192, 13, 7, 120));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(256, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(0, trace.discontinuities[1]);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 16, 7, 170));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 210));
    TEST_ASSERT_INT_EQ(576, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.discontinuities[2]);
    TEST_ASSERT_INT_EQ(1, trace.synthetic_discontinuity);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 10000));
    TEST_ASSERT_INT_EQ(576, trace.synthetic_samples);

    /* A timely expected packet repairs a full window without any concealment. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    for (uint16_t seq = 12; seq <= 15; seq++)
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(
            &jitter, samples, 160, seq, 7, 101U + seq - 12U));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 192, 11, 7, 105));
    TEST_ASSERT_INT_EQ(6, trace.real_count);
    for (int i = 0; i < 6; i++) TEST_ASSERT_INT_EQ(10 + i, trace.sequences[i]);
    TEST_ASSERT_INT_EQ(192, (int)trace.real_samples[1]);
    TEST_ASSERT_INT_EQ(105, (int)trace.timestamps[1]);
    TEST_ASSERT_INT_EQ(105, (int)trace.timestamps[5]);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(0, (int)jitter.late_packets);

    /* Fifth waiting packet forces the nearest queued successor, not slot order. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 16, 7, 101));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 102));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 15, 7, 103));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 14, 7, 104));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 17, 7, 105));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(12, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 140));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 141));
    TEST_ASSERT_INT_EQ(6, trace.real_count);
    TEST_ASSERT_INT_EQ(14, trace.sequences[2]);
    TEST_ASSERT_INT_EQ(15, trace.sequences[3]);
    TEST_ASSERT_INT_EQ(16, trace.sequences[4]);
    TEST_ASSERT_INT_EQ(17, trace.sequences[5]);
    TEST_ASSERT_INT_EQ(320, trace.synthetic_samples);

    /* Duplicate, stale and half-range packets never emit or reset ownership. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 256, 10, 7, 100));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 128, 10, 7, 101));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 128, 12, 7, 102));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 192, 12, 7, 103));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 32779, 7, 104));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 9, 7, 105));
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 8, 999));
    TEST_ASSERT_INT_EQ(2, (int)jitter.duplicate_packets);
    TEST_ASSERT_INT_EQ(2, (int)jitter.late_packets);
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 99));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_tick(&jitter, 99));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(&jitter, samples, 257, 11, 7, 106));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(&jitter, NULL, 160, 11, 7, 106));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(&jitter, samples, 0, 11, 7, 106));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 0, 106));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 106));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);

    /* uint16 wrap is contiguous; a huge positive gap emits only 320 samples. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 128, 65535, 7, 0));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 192, 0, 7, 1));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 32767, 7, 2));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 42));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(65535, trace.sequences[0]);
    TEST_ASSERT_INT_EQ(0, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(32767, trace.sequences[2]);
    TEST_ASSERT_INT_EQ(320, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.discontinuities[2]);

    /* Input bytes are owned until emission and reset discards queued packets. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 120));
    memset(samples, 0, sizeof(samples));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
    memset(samples, 0x23, sizeof(samples));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 14, 7, 170));
    df_gvs_audio_jitter_reset(&jitter, 8);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 1000));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 14, 7, 1001));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 50, 8, 1001));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(8, (int)trace.generations[2]);
    TEST_ASSERT_INT_EQ(50, trace.sequences[2]);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 52, 8, 1002));
    df_gvs_audio_jitter_reset(&jitter, 0);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 2000));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 70, 9, 2001));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    df_gvs_audio_jitter_reset(&jitter, 9);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 70, 9, 2002));
    TEST_ASSERT_INT_EQ(4, trace.real_count);
    TEST_ASSERT_INT_EQ(70, trace.sequences[3]);

    /* Saturating deadlines near UINT64_MAX must not wrap into the past. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, UINT64_MAX - 30));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, UINT64_MAX - 20));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, UINT64_MAX - 1));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, UINT64_MAX));
    TEST_ASSERT_INT_EQ(2, trace.real_count);

    memset(&trace, 0, sizeof(trace));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_init(NULL, record_emission, &trace));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_init(&jitter, NULL, &trace));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_init(&jitter, record_emission, &trace));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 0));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 1, 9, 0));
    TEST_ASSERT_INT_EQ(9, (int)trace.generations[0]);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_tick(NULL, 0));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_push(NULL, samples, 160, 1, 9, 0));
    df_gvs_audio_jitter_reset(NULL, 0);
}

void test_gvs_audio_jitter_preserves_partial_callback_progress(void)
{
    struct df_gvs_audio_jitter jitter;
    struct trace trace;
    uint8_t samples[160];
    memset(samples, 0x23, sizeof(samples));

    /* A failed real callback cannot consume the first sequence or its bytes. */
    start(&jitter, &trace);
    trace.fail_real = 1;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, trace.real_count);
    TEST_ASSERT_INT_EQ(10, jitter.expected_sequence);
    memset(samples, 0, sizeof(samples));
    trace.fail_real = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 101));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(10, trace.sequences[0]);
    TEST_ASSERT_INT_EQ(101, (int)trace.timestamps[0]);
    memset(samples, 0x23, sizeof(samples));

    /* Silence succeeds, real fails: retry must emit that real exactly once. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 14, 7, 120));
    trace.fail_real = 1;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(320, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(11, jitter.expected_sequence);
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 161));
    TEST_ASSERT_INT_EQ(1, (int)jitter.late_packets);
    trace.fail_real = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 162));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(14, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(162, (int)trace.timestamps[1]);
    TEST_ASSERT_INT_EQ(1, trace.discontinuities[1]);
    TEST_ASSERT_INT_EQ(320, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.synthetic_count);

    /* A failed silence callback can retry or be replaced by the missing real. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 120));
    trace.fail_synthetic = 1;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_tick(&jitter, 160));
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    trace.fail_synthetic = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 161));
    TEST_ASSERT_INT_EQ(3, trace.real_count);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);

    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 12, 7, 120));
    trace.fail_synthetic = 1;
    trace.failure = 4;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_jitter_tick(&jitter, 160));
    trace.fail_synthetic = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 161));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);

    /* Failed direct expected input is not retained in a fifth waiting slot. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    for (uint16_t seq = 12; seq <= 15; seq++)
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(
            &jitter, samples, 160, seq, 7, 101U + seq - 12U));
    trace.fail_real = 1;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 105));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(11, jitter.expected_sequence);
    int occupied = 0;
    for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++)
        occupied += jitter.slots[i].occupied;
    TEST_ASSERT_INT_EQ(4, occupied);
    trace.fail_real = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 106));
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 107));
    TEST_ASSERT_INT_EQ(6, trace.real_count);
    for (int i = 0; i < 6; i++) TEST_ASSERT_INT_EQ(10 + i, trace.sequences[i]);
    TEST_ASSERT_INT_EQ(107, (int)trace.timestamps[1]);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);

    /* If input succeeded but a queued real failed, only queued audio retries. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    for (uint16_t seq = 12; seq <= 15; seq++)
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(
            &jitter, samples, 160, seq, 7, 101U + seq - 12U));
    trace.fail_specific_real = true;
    trace.fail_sequence = 12;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 105));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(11, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(12, jitter.expected_sequence);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);
    occupied = 0;
    for (size_t i = 0; i < DF_GVS_AUDIO_JITTER_SLOTS; i++)
        occupied += jitter.slots[i].occupied;
    TEST_ASSERT_INT_EQ(4, occupied);
    trace.fail_specific_real = false;
    TEST_ASSERT_INT_EQ(1, df_gvs_audio_jitter_push(&jitter, samples, 160, 11, 7, 106));
    TEST_ASSERT_INT_EQ(2, trace.real_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 107));
    TEST_ASSERT_INT_EQ(6, trace.real_count);
    for (int i = 0; i < 6; i++) TEST_ASSERT_INT_EQ(10 + i, trace.sequences[i]);
    TEST_ASSERT_INT_EQ(105, (int)trace.timestamps[1]);
    TEST_ASSERT_INT_EQ(107, (int)trace.timestamps[2]);
    TEST_ASSERT_INT_EQ(0, trace.synthetic_samples);

    /* Failed full-window advancement retains four packets for a later retry. */
    start(&jitter, &trace);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 10, 7, 100));
    for (uint16_t seq = 12; seq <= 15; seq++)
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, seq, 7, 101));
    trace.fail_real = 1;
    TEST_ASSERT_INT_EQ(-9, df_gvs_audio_jitter_push(&jitter, samples, 160, 16, 7, 102));
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(1, trace.real_count);
    trace.fail_real = 0;
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 103));
    TEST_ASSERT_INT_EQ(5, trace.real_count);
    TEST_ASSERT_INT_EQ(12, trace.sequences[1]);
    TEST_ASSERT_INT_EQ(15, trace.sequences[4]);
    TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(&jitter, samples, 160, 16, 7, 104));
    TEST_ASSERT_INT_EQ(6, trace.real_count);
}

#ifdef DF_JITTER_TEST_PROGRAM
int df_test_failure_count;
int main(void)
{
    test_gvs_audio_jitter_reorders_and_bounds_gaps();
    test_gvs_audio_jitter_preserves_partial_callback_progress();
    return test_failures();
}
#endif
