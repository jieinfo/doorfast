#include <string.h>

#include "doorfast.h"
#include "media_frame_queue.h"
#include "test.h"

void test_media_queue_discards_oldest_and_never_aliases_reassembly_memory(void)
{
    struct df_media_frame_queue queue;
    struct df_media_frame frame;
    uint8_t first[] = {1};
    uint8_t second[] = {2};
    uint8_t third[] = {3};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(&queue, 2, 16));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, first, sizeof(first), 7, 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, second, sizeof(second), 7, 11));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, third, sizeof(third), 7, 12));
    second[0] = 9;
    TEST_ASSERT_INT_EQ(1, (int)queue.dropped_oldest);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(1, (int)frame.length);
    TEST_ASSERT_INT_EQ(2, frame.data[0]);
    TEST_ASSERT_INT_EQ(7, (int)frame.generation);
    TEST_ASSERT_INT_EQ(11, (int)frame.timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(3, frame.data[0]);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_frame_queue_pop(&queue, &frame));
    df_media_frame_queue_destroy(&queue);
}

void test_media_queue_rejects_stale_generation_and_releases_allocations(void)
{
    struct df_media_frame_queue queue;
    struct df_media_frame frame;
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(&queue, 4, sizeof(jpeg)));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, jpeg, sizeof(jpeg), 8, 20));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_frame_queue_push(&queue, jpeg, sizeof(jpeg), 9, 21));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_frame_queue_push(&queue, jpeg, sizeof(jpeg) + 1U, 8, 22));
    df_media_frame_queue_destroy(&queue);
    TEST_ASSERT_INT_EQ(0, (int)queue.capacity);
    TEST_ASSERT_INT_EQ(0, (int)queue.count);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_frame_queue_pop(&queue, &frame));
}
