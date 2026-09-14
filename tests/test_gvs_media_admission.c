#include <string.h>

#include "gvs_media_admission.h"
#include "test.h"

void test_gvs_media_admission_requires_current_exact_endpoints(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    uint8_t address[6];
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };

    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_ACCEPTED,
        df_gvs_media_admit(&session, local, local, peer));
    session.state = DF_GVS_PREVIEW;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_ACCEPTED,
        df_gvs_media_admit(&session, local, local, peer));
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_ACCEPTED,
        df_gvs_media_admit(&session, local, local, peer));

    memcpy(address, local, sizeof(address));
    address[5]++;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_DESTINATION,
        df_gvs_media_admit(&session, local, address, peer));
    memcpy(address, peer, sizeof(address));
    address[5]++;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_PEER,
        df_gvs_media_admit(&session, local, local, address));

    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_DESTINATION,
        df_gvs_media_admit(&session, local, peer, local));
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_INACTIVE,
        df_gvs_media_admit(&session, local, local, peer));
    session.state = DF_GVS_IDLE;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_INACTIVE,
        df_gvs_media_admit(&session, local, local, peer));
    session.state = DF_GVS_RINGING;
    session.generation = 0;
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_REJECT_INACTIVE,
        df_gvs_media_admit(&session, local, local, peer));
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_ADMISSION_ERROR,
        df_gvs_media_admit(NULL, local, local, peer));
}
