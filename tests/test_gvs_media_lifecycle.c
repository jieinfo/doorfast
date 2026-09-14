#include "gvs_media_lifecycle.h"
#include "test.h"

void test_gvs_media_lifecycle_isolates_generations_and_endings(void)
{
    struct df_gvs_media_lifecycle lifecycle;
    struct df_gvs_media_lifecycle_result result;
    struct df_gvs_session session = {0};

    df_gvs_media_lifecycle_init(&lifecycle);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(1, result.clear);
    TEST_ASSERT_INT_EQ(0, result.active);
    TEST_ASSERT_INT_EQ(0, result.started);
    TEST_ASSERT_INT_EQ(0, result.ended);

    session.state = DF_GVS_RINGING;
    session.generation = 7;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(1, result.clear);
    TEST_ASSERT_INT_EQ(1, result.active);
    TEST_ASSERT_INT_EQ(1, result.started);
    TEST_ASSERT_INT_EQ(0, result.ended);
    TEST_ASSERT_INT_EQ(7, (int)result.generation);

    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(0, result.clear);
    TEST_ASSERT_INT_EQ(0, result.started);
    TEST_ASSERT_INT_EQ(0, result.ended);

    session.state = DF_GVS_RINGING;
    session.generation = 8;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(1, result.clear);
    TEST_ASSERT_INT_EQ(1, result.started);
    TEST_ASSERT_INT_EQ(1, result.ended);
    TEST_ASSERT_INT_EQ(8, (int)result.generation);

    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(1, result.clear);
    TEST_ASSERT_INT_EQ(0, result.active);
    TEST_ASSERT_INT_EQ(0, result.started);
    TEST_ASSERT_INT_EQ(1, result.ended);
    TEST_ASSERT_INT_EQ(0, (int)result.generation);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_lifecycle_sync(
        &lifecycle, &session, &result));
    TEST_ASSERT_INT_EQ(0, result.clear);
}
