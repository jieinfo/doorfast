#include "test.h"
#include "gvs_audio_buffer.h"
#include <string.h>
void test_gvs_audio_buffer(void) {
    struct df_gvs_audio_buffer b;
    struct df_gvs_audio_buffer incremental;
    struct df_gvs_audio_status s;
    uint8_t out[8]; size_t n;
    const uint8_t a[] = {1, 2, 3}, c[] = {4, 5};
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(&b, a, sizeof(a), 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(&b, c, sizeof(c), 12, 7, 120));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&b, &s));
    TEST_ASSERT_INT_EQ(5, (int)s.buffered_bytes);
    TEST_ASSERT_INT_EQ(1, (int)s.sequence_gaps);
    TEST_ASSERT_INT_EQ(1, (int)s.missing_packets);
    TEST_ASSERT_INT_EQ(0, s.snapshot_ready);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_buffer_mark_snapshot(
        &b, 8, 5, 125));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(
        &b, 7, 5, 125));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&b, &s));
    TEST_ASSERT_INT_EQ(1, s.snapshot_ready);
    TEST_ASSERT_INT_EQ(2, (int)s.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(54, (int)s.snapshot_bytes);
    TEST_ASSERT_INT_EQ(125, (int)s.snapshot_timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_DUPLICATE,
        df_gvs_audio_buffer_push(&b, c, sizeof(c), 12, 7, 121));
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_LATE,
        df_gvs_audio_buffer_push(&b, c, sizeof(c), 11, 7, 122));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&b, &s));
    TEST_ASSERT_INT_EQ(1, (int)s.duplicate_packets);
    TEST_ASSERT_INT_EQ(1, (int)s.late_packets);
    TEST_ASSERT_INT_EQ(2, (int)s.packet_count);
    TEST_ASSERT_INT_EQ(5, (int)s.buffered_bytes);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy(&b, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(5, (int)n);
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\1\2\3\4\5", 5));
    TEST_ASSERT_INT_EQ(5, (int)b.length);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_read(&b, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(5, (int)n);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, a, sizeof(a), UINT16_MAX, 8, 200));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, c, sizeof(c), 0, 8, 220));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, a, sizeof(a), 2, 8, 240));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&b, &s));
    TEST_ASSERT_INT_EQ(8, (int)s.generation);
    TEST_ASSERT_INT_EQ(0, s.snapshot_ready);
    TEST_ASSERT_INT_EQ(0, (int)s.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(0, (int)s.snapshot_bytes);
    TEST_ASSERT_INT_EQ(3, (int)s.packet_count);
    TEST_ASSERT_INT_EQ(1, (int)s.sequence_gaps);
    TEST_ASSERT_INT_EQ(1, (int)s.missing_packets);
    TEST_ASSERT_INT_EQ(0, (int)s.duplicate_packets);
    TEST_ASSERT_INT_EQ(0, (int)s.late_packets);
    TEST_ASSERT_INT_EQ(8, (int)s.buffered_bytes);
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_ERROR,
        df_gvs_audio_buffer_push(&b, a, sizeof(a), 3, 0, 260));
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_STALE,
        df_gvs_audio_buffer_push(&b, c, sizeof(c), 3, 7, 261));
    TEST_ASSERT_INT_EQ(8, (int)b.generation);
    TEST_ASSERT_INT_EQ(8, (int)b.length);

    df_gvs_audio_buffer_init(&incremental);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &incremental, a, sizeof(a), 1, 9, 300));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending(
        &incremental, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(3, (int)n);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(
        &incremental, 9, n, 301));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending(
        &incremental, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(0, (int)n);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &incremental, c, sizeof(c), 2, 9, 320));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending(
        &incremental, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(2, (int)n);
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\4\5", 2));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(
        &incremental, 9, n, 321));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&incremental, &s));
    TEST_ASSERT_INT_EQ(2, (int)s.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(1, (int)s.snapshot_previous_packet_count);
    TEST_ASSERT_INT_EQ(5, (int)s.snapshot_source_bytes);
    TEST_ASSERT_INT_EQ(0, (int)s.snapshot_dropped_bytes);
    TEST_ASSERT_INT_EQ(48, (int)s.snapshot_bytes);
}

/* Catches synthetic samples being counted as source packets/bytes or omitted
 * from pending samples and the PCM WAV length. */
void test_gvs_audio_buffer_sample_clock(void) {
    struct df_gvs_audio_buffer buffer;
    struct df_gvs_audio_status status;
    uint8_t real[160], out[320];
    size_t length;
    memset(real, 0xd5, sizeof(real));
    df_gvs_audio_buffer_init(&buffer);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &buffer, real, 160, 10, 7, 100));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push_silence(&buffer, 160, 7));
    TEST_ASSERT_INT_EQ(160, (int)buffer.byte_count);
    TEST_ASSERT_INT_EQ(320, (int)buffer.sample_count);
    TEST_ASSERT_INT_EQ(160, (int)buffer.concealed_samples);
    TEST_ASSERT_INT_EQ(1, (int)buffer.packet_count);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending_tail(
        &buffer, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(320, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(out, real, 160));
    TEST_ASSERT_INT_EQ(0xd5, out[319]);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(
        &buffer, 7, length, 200));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending_tail(
        &buffer, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(0, (int)length);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&buffer, &status));
    TEST_ASSERT_INT_EQ(320, (int)status.sample_count);
    TEST_ASSERT_INT_EQ(320, (int)status.snapshot_source_samples);
    TEST_ASSERT_INT_EQ(160, (int)status.concealed_samples);
    TEST_ASSERT_INT_EQ(160, (int)status.snapshot_source_bytes);
    TEST_ASSERT_INT_EQ(1, (int)status.snapshot_packet_count);
    TEST_ASSERT_INT_EQ(684, (int)status.snapshot_bytes);
}

/* Catches selecting the oldest truncated prefix instead of the latest tail,
 * including overwrite and a publication cursor inside a wrapped ring. */
void test_gvs_audio_buffer_pending_tail(void) {
    struct df_gvs_audio_buffer b;
    uint8_t full[DF_GVS_AUDIO_BUFFER_CAPACITY], out[3];
    const uint8_t newer[] = {11, 12, 13, 14};
    size_t i, length;
    for (i = 0; i < sizeof(full); i++) full[i] = (uint8_t)i;
    df_gvs_audio_buffer_init(&b);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, full, sizeof(full), 1, 1, 0));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, newer, sizeof(newer), 2, 1, 1));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending(
        &b, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\4\5\6", 3));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending_tail(
        &b, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(3, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\14\15\16", 3));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_mark_snapshot(&b, 1, 3, 2));
    TEST_ASSERT_INT_EQ(65537, (int)b.snapshot_dropped_bytes);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending_tail(
        &b, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(0, (int)length);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
        &b, newer, sizeof(newer), 3, 1, 3));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy_pending_tail(
        &b, out, sizeof(out), &length));
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\14\15\16", 3));
}

/* Catches wrapped counters, partial mutation on error, and implicit revival
 * after closing a session. */
void test_gvs_audio_buffer_limits_and_close(void) {
    struct df_gvs_audio_buffer b, before;
    uint8_t real = 0xd5, out;
    size_t length;
    unsigned int i;
    for (i = 0; i < 3; i++) {
        df_gvs_audio_buffer_reset(&b, 7);
        if (i == 0) b.byte_count = UINT64_MAX;
        if (i == 1) b.sample_count = UINT64_MAX;
        if (i == 2) b.packet_count = UINT64_MAX;
        before = b;
        TEST_ASSERT_INT_EQ(-1, df_gvs_audio_buffer_push(&b, &real, 1, 1, 7, 0));
        TEST_ASSERT_INT_EQ(0, memcmp(&b, &before, sizeof(b)));
    }
    for (i = 0; i < 2; i++) {
        df_gvs_audio_buffer_reset(&b, 7);
        if (i == 0) b.sample_count = UINT64_MAX;
        if (i == 1) b.concealed_samples = UINT64_MAX;
        before = b;
        TEST_ASSERT_INT_EQ(-1, df_gvs_audio_buffer_push_silence(&b, 1, 7));
        TEST_ASSERT_INT_EQ(0, memcmp(&b, &before, sizeof(b)));
    }
    df_gvs_audio_buffer_reset(&b, 7);
    b.snapshot_source_samples = 1;
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_buffer_copy_pending_tail(&b, &out, 1, &length));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_buffer_mark_snapshot(&b, 7, 1, 0));
    df_gvs_audio_buffer_reset(&b, 0);
    before = b;
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_STALE,
        df_gvs_audio_buffer_push(&b, &real, 1, 1, 8, 0));
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_BUFFER_STALE,
        df_gvs_audio_buffer_push_silence(&b, 1, 8));
    TEST_ASSERT_INT_EQ(0, memcmp(&b, &before, sizeof(b)));
    df_gvs_audio_buffer_reset(&b, 8);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(&b, &real, 1, 1, 8, 0));
}
