#include <string.h>

#include "gvs_video_frame_cache.h"
#include "test.h"

void test_gvs_video_frame_cache(void)
{
    struct df_gvs_video_frame_cache cache;
    struct df_gvs_video_status status;
    const uint8_t *data;
    size_t length;
    uint64_t generation;
    uint64_t time;
    uint16_t frame_no;
    const uint8_t frame[] = {0xff, 0xd8, 0xff, 0xd9};

    df_gvs_video_frame_cache_init(&cache);
    TEST_ASSERT_INT_EQ(-1, df_gvs_video_frame_cache_snapshot(
        &cache, &data, &length, &generation, &frame_no, &time));
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_status(&cache, &status));
    TEST_ASSERT_INT_EQ(0, status.ready);
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_store(
        &cache, frame, sizeof(frame), 9, 42, 123));
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_snapshot(
        &cache, &data, &length, &generation, &frame_no, &time));
    TEST_ASSERT_INT_EQ(4, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(data, frame, 4));
    TEST_ASSERT_INT_EQ(9, (int)generation);
    TEST_ASSERT_INT_EQ(42, frame_no);
    TEST_ASSERT_INT_EQ(123, (int)time);
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_status(&cache, &status));
    TEST_ASSERT_INT_EQ(1, status.ready);
    TEST_ASSERT_INT_EQ(4, (int)status.bytes);
    TEST_ASSERT_INT_EQ(9, (int)status.generation);
    TEST_ASSERT_INT_EQ(42, status.frame_no);
    TEST_ASSERT_INT_EQ(123, (int)status.timestamp_ms);
    TEST_ASSERT_INT_EQ(-1, df_gvs_video_frame_cache_store(
        &cache, frame, sizeof(frame), 0, 43, 124));
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_status(&cache, &status));
    TEST_ASSERT_INT_EQ(42, status.frame_no);
    df_gvs_video_frame_cache_invalidate(&cache);
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_status(&cache, &status));
    TEST_ASSERT_INT_EQ(0, status.ready);
    TEST_ASSERT_INT_EQ(1, cache.capacity >= sizeof(frame));
    df_gvs_video_frame_cache_reset(&cache);
    TEST_ASSERT_INT_EQ(0, df_gvs_video_frame_cache_status(&cache, &status));
    TEST_ASSERT_INT_EQ(0, status.ready);
}
