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
    uint8_t fourth[] = {4};
    uint8_t fifth[] = {5};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(&queue, 7, 16));
    TEST_ASSERT_INT_EQ(DF_MEDIA_FRAME_QUEUE_CAPACITY, (int)queue.capacity);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, first, sizeof(first), 7, 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, second, sizeof(second), 7, 11));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, third, sizeof(third), 7, 12));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, fourth, sizeof(fourth), 7, 13));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_frame_queue_push(&queue, fifth, sizeof(fifth), 7, 14));
    second[0] = 9;
    TEST_ASSERT_INT_EQ(1, (int)queue.dropped_oldest);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(1, (int)frame.length);
    TEST_ASSERT_INT_EQ(2, frame.data[0]);
    TEST_ASSERT_INT_EQ(7, (int)frame.generation);
    TEST_ASSERT_INT_EQ(11, (int)frame.timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(3, frame.data[0]);
    TEST_ASSERT_INT_EQ(12, (int)frame.timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(4, frame.data[0]);
    TEST_ASSERT_INT_EQ(13, (int)frame.timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&queue, &frame));
    TEST_ASSERT_INT_EQ(5, frame.data[0]);
    TEST_ASSERT_INT_EQ(14, (int)frame.timestamp_ms);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_frame_queue_pop(&queue, &frame));
    df_media_frame_queue_destroy(&queue);
}

void test_media_queue_rejects_stale_generation_and_releases_allocations(void)
{
    struct df_media_frame_queue queue;
    struct df_media_frame frame;
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_frame_queue_init(&queue, 0, sizeof(jpeg)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(&queue, 8, sizeof(jpeg)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_media_frame_queue_push(&queue, jpeg, sizeof(jpeg), 7, 19));
    TEST_ASSERT_INT_EQ(0, (int)queue.count);
    TEST_ASSERT_INT_EQ(8, (int)queue.generation);
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

void test_media_queue_resets_only_its_generation_state(void)
{
    struct df_media_frame_queue first;
    struct df_media_frame_queue second;
    struct df_media_frame frame;
    const uint8_t old_frame[] = {0xff, 0xd8, 0x01, 0xff, 0xd9};
    const uint8_t other_frame[] = {0xff, 0xd8, 0x02, 0xff, 0xd9};
    const uint8_t new_frame[] = {0xff, 0xd8, 0x03, 0xff, 0xd9};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(
        &first, 10U, sizeof(old_frame)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_init(
        &second, 20U, sizeof(other_frame)));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(
        &first, old_frame, sizeof(old_frame), 10U, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(
        &second, other_frame, sizeof(other_frame), 20U, 101U));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_reset(&first, 11U));
    TEST_ASSERT_INT_EQ(0, (int)first.count);
    TEST_ASSERT_INT_EQ(11, (int)first.generation);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_frame_queue_push(
        &first, old_frame, sizeof(old_frame), 10U, 102U));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(
        &first, new_frame, sizeof(new_frame), 11U, 103U));

    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&second, &frame));
    TEST_ASSERT_INT_EQ(2, frame.data[2]);
    TEST_ASSERT_INT_EQ(20, (int)frame.generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_pop(&first, &frame));
    TEST_ASSERT_INT_EQ(3, frame.data[2]);
    TEST_ASSERT_INT_EQ(11, (int)frame.generation);

    df_media_frame_queue_destroy(&first);
    df_media_frame_queue_destroy(&second);
}
