#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "doorfast.h"
#include "media_session.h"
#include "test.h"

static const struct df_media_station_config_v3 preview_station = {
    .id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
    .logical_address = {0U, 1U, 2U, 3U, 4U, 5U},
};
static const uint8_t preview_jpeg[] = {
    0xffU, 0xd8U, 0xffU, 0xe0U, 0x00U, 0x04U, 0x00U, 0x00U,
    0xffU, 0xc0U, 0x00U, 0x08U, 0x08U, 0x01U, 0xe0U, 0x02U,
    0x80U, 0x00U, 0xffU, 0xd9U,
};

static void preview_session_with_encoder(struct df_media_session *session,
    enum df_media_session_purpose purpose) {
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_publish(session,
        &preview_station, 0x01020304U, purpose, 7U, 100U));
    session->encoder.input_fd = open("/dev/null", O_WRONLY);
    TEST_ASSERT_INT_EQ(1, session->encoder.input_fd >= 0);
    session->encoder.input_owned = true;
    session->encoder.running = true;
    session->encoder.generation = 7U;
    session->encoder.source_width = 640U;
    session->encoder.source_height = 480U;
}

void test_media_session_preview_publisher_survives_source_attempt(void) {
    struct df_media_session session = {0};
    struct df_media_module_config_v3 config = {.fps = 8U};
    pid_t publisher_pid;
    uint64_t frames_written;

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_publish(&session,
        &preview_station, 0x01020304U, DF_MEDIA_SESSION_PREVIEW, 7U, 100U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_WAITING, session.source_state);
    df_media_session_mark_source_lost(&session, 101U);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 102U));
    TEST_ASSERT_INT_EQ(0, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(0, session.publication_generation);

    session.encoder.input_fd = open("/dev/null", O_WRONLY);
    TEST_ASSERT_INT_EQ(1, session.encoder.input_fd >= 0);
    session.encoder.input_owned = true;
    session.encoder.running = true;
    session.encoder.generation = 7U;
    session.encoder.source_width = 640U;
    session.encoder.source_height = 480U;
    publisher_pid = fork();
    if (publisher_pid == 0) {
        for (;;) pause();
    }
    TEST_ASSERT_INT_EQ(1, publisher_pid > 0);
    if (publisher_pid <= 0) {
        (void)df_media_session_stop_pipeline(&session);
        df_media_session_reset(&session);
        return;
    }
    session.encoder.pid = publisher_pid;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_push_jpeg(&session, &config,
        NULL, preview_jpeg, sizeof(preview_jpeg), 640U, 480U, 200U));
    TEST_ASSERT_INT_EQ(7, session.publication_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_LIVE, session.source_state);
    TEST_ASSERT_INT_EQ(1, session.frames_received);
    TEST_ASSERT_INT_EQ(1, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(1, session.encoder.frames_written);

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_reset_attempt(&session, 8U));
    df_media_session_mark_source_lost(&session, 300U);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_RECONNECTING,
        session.source_state);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 300U));
    TEST_ASSERT_INT_EQ(7, session.encoder.generation);
    TEST_ASSERT_INT_EQ((int)publisher_pid, (int)session.encoder.pid);
    TEST_ASSERT_INT_EQ(7, session.publication_generation);
    TEST_ASSERT_INT_EQ(8, session.media_generation);
    TEST_ASSERT_INT_EQ(1, session.frames_received);
    TEST_ASSERT_INT_EQ(200, session.last_frame_ms);
    TEST_ASSERT_INT_EQ(2, session.encoder.frames_written);
    df_media_session_mark_source_lost(&session, 310U);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 310U));
    TEST_ASSERT_INT_EQ(2, session.encoder.frames_written);
    frames_written = session.encoder.frames_written;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 424U));
    TEST_ASSERT_INT_EQ((int)frames_written,
        (int)session.encoder.frames_written);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 425U));
    TEST_ASSERT_INT_EQ(3, session.encoder.frames_written);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_push_jpeg(&session, &config,
        NULL, preview_jpeg, sizeof(preview_jpeg), 640U, 480U, 426U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_LIVE, session.source_state);
    TEST_ASSERT_INT_EQ(2, session.frames_received);
    TEST_ASSERT_INT_EQ(426, session.last_frame_ms);
    TEST_ASSERT_INT_EQ(7, session.encoder.generation);
    TEST_ASSERT_INT_EQ((int)publisher_pid, (int)session.encoder.pid);
    TEST_ASSERT_INT_EQ(4, session.encoder.frames_written);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_stop_pipeline(&session));
    TEST_ASSERT_INT_EQ(0, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(0, session.publication_generation);
    df_media_session_reset(&session);
}

void test_media_session_call_never_sends_reconnect_frame(void) {
    struct df_media_session session = {0};
    struct df_media_module_config_v3 config = {.fps = 8U};

    preview_session_with_encoder(&session, DF_MEDIA_SESSION_CALL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_push_jpeg(&session, &config,
        NULL, preview_jpeg, sizeof(preview_jpeg), 640U, 480U, 200U));
    df_media_session_mark_source_lost(&session, 300U);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_tick_reconnect(&session, 8U, 300U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_LIVE, session.source_state);
    TEST_ASSERT_INT_EQ(1, session.encoder.frames_written);
    TEST_ASSERT_INT_EQ(0, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_stop_pipeline(&session));
    df_media_session_reset(&session);
}

void test_media_session_reconnect_rejects_dimension_change(void) {
    struct df_media_session session = {0};
    struct df_media_module_config_v3 config = {.fps = 8U};

    preview_session_with_encoder(&session, DF_MEDIA_SESSION_PREVIEW);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_push_jpeg(&session, &config,
        NULL, preview_jpeg, sizeof(preview_jpeg), 640U, 480U, 200U));
    df_media_session_mark_source_lost(&session, 300U);
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_session_push_jpeg(&session,
        &config, NULL, preview_jpeg, sizeof(preview_jpeg), 800U, 600U,
        301U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_FAILED, session.state);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_RECONNECT_FRAME, session.last_error);
    TEST_ASSERT_INT_EQ(0, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(0, session.publication_generation);
    TEST_ASSERT_INT_EQ(0, df_media_encoder_is_running(&session.encoder));
    df_media_session_reset(&session);
}

void test_media_session_encoder_exit_releases_reconnect_frame(void) {
    struct df_media_session session = {0};
    struct df_media_module_config_v3 config = {.fps = 8U};

    preview_session_with_encoder(&session, DF_MEDIA_SESSION_PREVIEW);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_push_jpeg(&session, &config,
        NULL, preview_jpeg, sizeof(preview_jpeg), 640U, 480U, 200U));
    session.encoder.encoder_exited = true;
    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_tick_pipeline(&session, 201U));
    TEST_ASSERT_INT_EQ(0, session.reconnect_frame.data != NULL);
    TEST_ASSERT_INT_EQ(0, session.publication_generation);
    df_media_session_reset(&session);
}

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

void test_media_session_viewer_release_preserves_publishing_state(void) {
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
    struct df_media_session session = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_publish(&session, &station,
        0x01020304U, DF_MEDIA_SESSION_PREVIEW, 7U, 100U));
    session.state = DF_MEDIA_SESSION_PUBLISHING;
    session.frames_received = 1U;
    session.encoder.running = true;

    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, true, 101U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_VIEWING, session.state);
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, false, 102U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PUBLISHING, session.state);
    TEST_ASSERT_INT_EQ(0, session.viewer_active);
}

void test_media_session_viewer_release_preserves_pre_media_state(void) {
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
    struct df_media_session session = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_media_session_publish(&session, &station,
        0x01020304U, DF_MEDIA_SESSION_PREVIEW, 7U, 100U));
    session.monitor.state = DF_GVS_MONITOR_REQUESTING;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, true, 101U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, false, 102U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_REQUESTING, session.state);

    session.state = DF_MEDIA_SESSION_AWAITING_VIDEO;
    session.monitor.state = DF_GVS_MONITOR_AWAITING_VIDEO;
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, true, 103U));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_media_session_command(&session, DF_MEDIA_MODULE_COMMAND_VIEWER,
            &current, false, 104U));
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_AWAITING_VIDEO, session.state);
}
