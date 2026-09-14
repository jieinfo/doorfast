#include "test.h"
#include "gvs_audio_buffer.h"
#include <string.h>
void test_gvs_audio_buffer(void) {
    struct df_gvs_audio_buffer b;
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
}
