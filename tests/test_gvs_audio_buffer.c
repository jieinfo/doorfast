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
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_copy(&b, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(5, (int)n);
    TEST_ASSERT_INT_EQ(0, memcmp(out, "\1\2\3\4\5", 5));
    TEST_ASSERT_INT_EQ(5, (int)b.length);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_read(&b, out, sizeof(out), &n));
    TEST_ASSERT_INT_EQ(5, (int)n);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(&b, a, sizeof(a), 1, 8, 200));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_status(&b, &s));
    TEST_ASSERT_INT_EQ(8, (int)s.generation);
    TEST_ASSERT_INT_EQ(1, (int)s.packet_count);
}
