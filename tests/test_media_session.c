#include <string.h>

#include "media_session.h"
#include "test.h"

void test_media_session_binds_commands_to_station_and_generation(void) {
    const struct df_media_station_config_v3 station = {
        .id = "gate_main",
        .stream_name = "doorfast_gate_main",
        .enabled = true,
        .logical_address = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U},
    };
    const struct df_media_session_key current = {
        .station_id = "gate_main",
        .generation = 7U,
    };
    const struct df_media_session_key stale = {
        .station_id = "gate_main",
        .generation = 6U,
    };
    struct df_media_session session = {0};

    df_media_session_reset(&session);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_publish(&session, &station,
        0x01020304U, DF_MEDIA_SESSION_PREVIEW, 7U, 100U));
    TEST_ASSERT_INT_EQ(1, session.active);
    TEST_ASSERT_INT_EQ(1, strcmp(session.station_id, "gate_main") == 0);
    TEST_ASSERT_INT_EQ(7, session.generation);
    TEST_ASSERT_INT_EQ(1, df_media_session_matches_key(&session, &current));
    TEST_ASSERT_INT_EQ(0, df_media_session_matches_key(&session, &stale));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &stale, true, 101U));
    TEST_ASSERT_INT_EQ(0, session.viewer_active);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, true, 102U));
    TEST_ASSERT_INT_EQ(1, session.viewer_active);
}
