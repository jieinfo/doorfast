#include "gvs_video_reassembly.h"
#include "test.h"

void test_gvs_jpeg(void) {
    const unsigned char good[] = {0xff, 0xd8, 0x01, 0xff, 0xd9};
    const unsigned char bad[] = {0xff, 0xd8, 0x01, 0x02, 0xd9};

    TEST_ASSERT_INT_EQ(0, df_gvs_jpeg_validate(good, sizeof(good)));
    TEST_ASSERT_INT_EQ(-1, df_gvs_jpeg_validate(bad, sizeof(bad)));
    TEST_ASSERT_INT_EQ(-1, df_gvs_jpeg_validate(good, 3));
}

void test_gvs_jpeg_extracts_dimensions_from_sof(void) {
    const uint8_t jpeg[] = {
        0xff, 0xd8,
        0xff, 0xe0, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xc0, 0x00, 0x08, 0x08, 0x02, 0x80, 0x01, 0xe0, 0x00,
        0xff, 0xd9,
    };
    const uint8_t truncated[] = {
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x08, 0x08, 0x02,
    };
    uint16_t width = 0;
    uint16_t height = 0;

    TEST_ASSERT_INT_EQ(0, df_gvs_jpeg_dimensions(
        jpeg, sizeof(jpeg), &width, &height));
    TEST_ASSERT_INT_EQ(480, width);
    TEST_ASSERT_INT_EQ(640, height);
    TEST_ASSERT_INT_EQ(-1, df_gvs_jpeg_dimensions(
        truncated, sizeof(truncated), &width, &height));
    TEST_ASSERT_INT_EQ(-1, df_gvs_jpeg_dimensions(
        jpeg, sizeof(jpeg), NULL, &height));
}
