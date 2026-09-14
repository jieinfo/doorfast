#include <string.h>

#include "gvs_audio_tx.h"
#include "gvs_media.h"
#include "test.h"

struct audio_tx_capture {
    uint8_t frame[DF_GVS_AUDIO_HEADER_LEN + DF_GVS_AUDIO_TX_SAMPLES];
    size_t length;
    unsigned calls;
    int result;
};

static int capture_audio_frame(const uint8_t *frame, size_t length,
                               void *context)
{
    struct audio_tx_capture *capture = context;

    capture->calls++;
    if (capture->result != 0) {
        return capture->result;
    }
    if (length > sizeof(capture->frame)) {
        return DF_ERR_IO;
    }
    memcpy(capture->frame, frame, length);
    capture->length = length;
    return DF_OK;
}

void test_gvs_audio_tx_binds_pcm_to_talking_generation_and_pacing(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };
    struct audio_tx_capture capture = {0};
    struct df_gvs_audio_tx tx;
    struct df_gvs_audio_tx_status status;
    struct df_gvs_audio_packet packet;
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES] = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &tx, 100, capture_audio_frame, &capture));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_audio_tx_start(
        &tx, &session, 7, local, 1000));
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_start(
        &tx, &session, 7, local, 1000));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_audio_tx_submit_pcm(
        &tx, 6, pcm, DF_GVS_AUDIO_TX_SAMPLES, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 1000));
    TEST_ASSERT_INT_EQ(1, capture.calls);
    TEST_ASSERT_INT_EQ(202, (int)capture.length);
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(
        capture.frame, capture.length, &packet));
    TEST_ASSERT_INT_EQ(100, packet.sequence);
    TEST_ASSERT_INT_EQ(160, (int)packet.payload_length);
    TEST_ASSERT_INT_EQ(0xd5, packet.payload[0]);
    TEST_ASSERT_INT_EQ(0, memcmp(packet.destination, session.peer, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(packet.source, local, 6));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 1019));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 1020));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(2, (int)status.packets_sent);
    TEST_ASSERT_INT_EQ(102, status.next_sequence);
    TEST_ASSERT_INT_EQ(1040, (int)status.next_send_ms);
}

void test_gvs_audio_tx_preserves_sequence_across_sessions_and_failures(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {
        .state = DF_GVS_TALKING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };
    struct audio_tx_capture capture = {0};
    struct df_gvs_audio_tx tx;
    struct df_gvs_audio_tx_status status;
    struct df_gvs_audio_packet packet;
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES] = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &tx, 65535, capture_audio_frame, &capture));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_start(
        &tx, &session, 7, local, 10));
    capture.result = DF_ERR_IO;
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(65535, status.next_sequence);
    TEST_ASSERT_INT_EQ(1, (int)status.packets_failed);
    capture.result = DF_OK;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 10));
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(
        capture.frame, capture.length, &packet));
    TEST_ASSERT_INT_EQ(65535, packet.sequence);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(0, status.next_sequence);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_audio_tx_stop(&tx, 6, 31));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_stop(&tx, 7, 31));

    session.generation = 8;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_start(
        &tx, &session, 8, local, 40));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_submit_pcm(
        &tx, 8, pcm, DF_GVS_AUDIO_TX_SAMPLES, 40));
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(
        capture.frame, capture.length, &packet));
    TEST_ASSERT_INT_EQ(0, packet.sequence);
}

void test_gvs_audio_tx_syncs_to_call_lifecycle(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };
    struct audio_tx_capture capture = {0};
    struct df_gvs_audio_tx tx;
    struct df_gvs_audio_tx_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &tx, 300, capture_audio_frame, &capture));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_sync(
        &tx, &session, local, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(0, status.active);
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_sync(
        &tx, &session, local, 101));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(1, status.active);
    TEST_ASSERT_INT_EQ(7, (int)status.generation);
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_sync(
        &tx, &session, local, 102));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(0, status.active);
    TEST_ASSERT_INT_EQ(300, status.next_sequence);
    session.state = DF_GVS_TALKING;
    session.generation = 8;
    session.peer[4] = 2;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_sync(
        &tx, &session, local, 103));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_read_status(&tx, &status));
    TEST_ASSERT_INT_EQ(1, status.active);
    TEST_ASSERT_INT_EQ(8, (int)status.generation);
}
