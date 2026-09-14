#include <string.h>

#include "gvs_video_reassembly.h"
#include "test.h"

void test_gvs_video_reassembly(void)
{
    struct df_gvs_video_reassembly reassembly;
    struct df_gvs_video_packet first = {
        .frame_no = 4,
        .chunk_count = 3,
        .chunk_index = 1,
        .chunk_length = 2,
        .full_length = 5,
        .capacity = 2,
        .payload = (const uint8_t *)"AB",
    };
    struct df_gvs_video_packet second = first;
    struct df_gvs_video_packet third = first;
    struct df_gvs_video_packet conflicting = first;
    struct df_gvs_video_packet late = first;
    const uint8_t *output;
    size_t output_length;

    second.chunk_index = 2;
    second.payload = (const uint8_t *)"CD";
    third.chunk_index = 3;
    third.chunk_length = 1;
    third.payload = (const uint8_t *)"E";
    conflicting.payload = (const uint8_t *)"AX";

    df_gvs_video_reassembly_init(&reassembly);
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &second, &output, &output_length));
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_DUPLICATE,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_ERROR,
        df_gvs_video_reassembly_push(
            &reassembly, &conflicting, &output, &output_length));
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_COMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &third, &output, &output_length));
    TEST_ASSERT_INT_EQ(5, (int)output_length);
    TEST_ASSERT_INT_EQ(0, memcmp(output, "ABCDE", 5));

    late.frame_no = 3;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_LATE,
        df_gvs_video_reassembly_push(
            &reassembly, &late, &output, &output_length));
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_DUPLICATE,
        df_gvs_video_reassembly_push(
            &reassembly, &third, &output, &output_length));

    first.frame_no = 5;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    TEST_ASSERT_INT_EQ(1, (int)reassembly.received_chunks);
    TEST_ASSERT_INT_EQ(5, (int)reassembly.frame_no);

    second.frame_no = 5;
    second.chunk_count = 2;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_ERROR,
        df_gvs_video_reassembly_push(
            &reassembly, &second, &output, &output_length));
    TEST_ASSERT_INT_EQ(1, (int)reassembly.received_chunks);

    df_gvs_video_reassembly_reset(&reassembly);
    first.frame_no = UINT16_MAX;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    first.frame_no = 0;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_INCOMPLETE,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    TEST_ASSERT_INT_EQ(0, (int)reassembly.frame_no);

    first.chunk_index = 0;
    TEST_ASSERT_INT_EQ(DF_GVS_VIDEO_REASSEMBLY_ERROR,
        df_gvs_video_reassembly_push(
            &reassembly, &first, &output, &output_length));
    df_gvs_video_reassembly_reset(&reassembly);
}
