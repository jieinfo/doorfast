#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

#include "doorfast.h"
#include "gvs_video_reassembly.h"
#include "media_reconnect_frame.h"
#include "test.h"

static void assert_decoded_text(const struct df_media_reconnect_frame *frame,
                                unsigned int width, unsigned int height)
{
    struct jpeg_decompress_struct decoder;
    struct jpeg_error_mgr error;
    unsigned char *row;
    unsigned int bright_pixels = 0;
    unsigned int background = 0;

    decoder.err = jpeg_std_error(&error);
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, frame->data, (unsigned long)frame->length);
    TEST_ASSERT_INT_EQ(JPEG_HEADER_OK, jpeg_read_header(&decoder, TRUE));
    TEST_ASSERT_INT_EQ((int)width, (int)decoder.image_width);
    TEST_ASSERT_INT_EQ((int)height, (int)decoder.image_height);
    decoder.out_color_space = JCS_RGB;
    TEST_ASSERT_INT_EQ(TRUE, jpeg_start_decompress(&decoder));
    row = malloc((size_t)width * 3U);
    TEST_ASSERT_INT_EQ(1, row != NULL);
    if (row != NULL) {
        while (decoder.output_scanline < decoder.output_height) {
            unsigned int y = decoder.output_scanline;
            JSAMPROW scanline = row;
            jpeg_read_scanlines(&decoder, &scanline, 1);
            if (y == 0U) background = row[0] + row[1] + row[2];
            if (y < height / 3U || y >= height * 2U / 3U) continue;
            for (unsigned int x = width / 4U; x < width * 3U / 4U; x++) {
                size_t pixel = (size_t)x * 3U;
                unsigned int brightness = row[pixel] + row[pixel + 1U] +
                                          row[pixel + 2U];
                if (brightness > background + 180U) bright_pixels++;
            }
        }
        free(row);
    }
    TEST_ASSERT_INT_EQ(1, bright_pixels > 100U);
    TEST_ASSERT_INT_EQ(TRUE, jpeg_finish_decompress(&decoder));
    jpeg_destroy_decompress(&decoder);
}

void test_media_reconnect_frame_dimensions_and_bounds(void)
{
    const uint16_t dimensions[][2] = {{640U, 480U}, {800U, 600U}};

    for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
        struct df_media_reconnect_frame frame = {0};
        uint16_t width = dimensions[i][0];
        uint16_t height = dimensions[i][1];

        TEST_ASSERT_INT_EQ(DF_OK,
            df_media_reconnect_frame_create(width, height, &frame));
        TEST_ASSERT_INT_EQ(width, frame.width);
        TEST_ASSERT_INT_EQ(height, frame.height);
        TEST_ASSERT_INT_EQ(1, frame.data != NULL);
        TEST_ASSERT_INT_EQ(1, frame.length > 0U &&
                              frame.length <= DF_GVS_VIDEO_MAX_FRAME);
        if (frame.data != NULL && frame.length > 0U) {
            TEST_ASSERT_INT_EQ(0, df_gvs_jpeg_validate(frame.data, frame.length));
            assert_decoded_text(&frame, width, height);
        }
        df_media_reconnect_frame_destroy(&frame);
        TEST_ASSERT_INT_EQ(1, frame.data == NULL);
        TEST_ASSERT_INT_EQ(0, frame.length);
    }

    for (size_t i = 0; i < 2U; i++) {
        struct df_media_reconnect_frame frame = {0};
        uint16_t width = i == 0U ? 0U : UINT16_MAX;
        uint16_t height = i == 0U ? 480U : UINT16_MAX;

        frame.data = (uint8_t *)(uintptr_t)1U;
        frame.length = 17U;
        frame.width = 7U;
        frame.height = 9U;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
            df_media_reconnect_frame_create(width, height, &frame));
        TEST_ASSERT_INT_EQ(1, frame.data == NULL);
        TEST_ASSERT_INT_EQ(0, frame.length);
        TEST_ASSERT_INT_EQ(0, frame.width);
        TEST_ASSERT_INT_EQ(0, frame.height);
    }
}

void test_media_reconnect_frame_freezes_complex_jpeg_within_bounds(void) {
    struct jpeg_compress_struct compressor;
    struct jpeg_error_mgr error;
    struct df_media_reconnect_frame frozen = {0};
    unsigned char row[1920U * 3U], *jpeg = NULL;
    unsigned long length = 0U;
    uint32_t noise = 7U;
    compressor.err = jpeg_std_error(&error);
    jpeg_create_compress(&compressor);
    jpeg_mem_dest(&compressor, &jpeg, &length);
    compressor.image_width = 1920U;
    compressor.image_height = 1080U;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 40, TRUE);
    jpeg_start_compress(&compressor, TRUE);
    while (compressor.next_scanline < 1080U) {
        JSAMPROW scanline = row;
        for (size_t i = 0U; i < sizeof(row); i++) {
            noise = noise * 1664525U + 1013904223U;
            row[i] = (uint8_t)(noise >> 24U);
        }
        jpeg_write_scanlines(&compressor, &scanline, 1U);
    }
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    TEST_ASSERT_INT_EQ(1, length > 0U && length < DF_GVS_VIDEO_MAX_FRAME);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_reconnect_frame_freeze(jpeg, length,
        1920U, 1080U, &frozen));
    TEST_ASSERT_INT_EQ(1, frozen.length > 0U &&
        frozen.length <= DF_GVS_VIDEO_MAX_FRAME);
    if (frozen.data != NULL) {
        TEST_ASSERT_INT_EQ(0, df_gvs_jpeg_validate(frozen.data, frozen.length));
        df_media_reconnect_frame_destroy(&frozen);
    }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_reconnect_frame_freeze(jpeg, length,
        640U, 480U, &frozen));
    TEST_ASSERT_INT_EQ(0, frozen.data != NULL);
    const uint8_t invalid[] = {0xff, 0xd8, 0xff, 0xd9};
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_reconnect_frame_freeze(invalid,
        sizeof(invalid), 640U, 480U, &frozen));
    TEST_ASSERT_INT_EQ(0, frozen.data != NULL);
    free(jpeg);
}

#ifdef DF_MEDIA_RECONNECT_FRAME_TEST_MAIN
int df_test_failure_count = 0;

int main(void)
{
    test_media_reconnect_frame_dimensions_and_bounds();
    test_media_reconnect_frame_freezes_complex_jpeg_within_bounds();
    return df_test_failure_count == 0 ? 0 : 1;
}
#endif
