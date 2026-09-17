#include "config.h"
#include "doorfast.h"
#include "media_capacity.h"
#include "test.h"

void test_media_capacity_cannot_exceed_verified_single_station_limit(void)
{
    TEST_ASSERT_INT_EQ(1, (int)df_media_effective_capacity(4, 4, 1));
    TEST_ASSERT_INT_EQ(1, (int)df_media_effective_capacity(0, 4, 1));
    TEST_ASSERT_INT_EQ(0, (int)df_media_effective_capacity(1, 0, 1));
}

void test_media_encoder_selection_falls_back_in_verified_order(void)
{
    const struct df_media_encoder_probe qsv_only = {
        .software_available = true,
        .vaapi_available = true,
        .qsv_available = true,
    };
    const struct df_media_encoder_probe vaapi_only = {
        .software_available = true,
        .vaapi_available = true,
        .qsv_available = false,
    };
    const struct df_media_encoder_probe software_only = {
        .software_available = true,
        .vaapi_available = false,
        .qsv_available = false,
    };
    const struct df_media_encoder_probe unavailable = {0};
    enum df_media_encoder selected = DF_MEDIA_ENCODER_AUTO;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_select(
        DF_MEDIA_ENCODER_AUTO, &qsv_only, &selected));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ENCODER_QSV, selected);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_select(
        DF_MEDIA_ENCODER_AUTO, &vaapi_only, &selected));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ENCODER_VAAPI, selected);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_encoder_select(
        DF_MEDIA_ENCODER_AUTO, &software_only, &selected));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ENCODER_SOFTWARE, selected);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_encoder_select(
        DF_MEDIA_ENCODER_QSV, &vaapi_only, &selected));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_encoder_select(
        DF_MEDIA_ENCODER_AUTO, &unavailable, &selected));
}
