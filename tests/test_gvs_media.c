#include "test.h"
#include "gvs_media.h"
#include <string.h>

static const unsigned char gvs_magic[10] = {
    0x47, 0x56, 0x53, 0x47, 0x56, 0x53, 0xa5, 0xa5, 0xa5, 0xa5
};

static void header(unsigned char *packet)
{
    memcpy(packet, gvs_magic, sizeof(gvs_magic));
}

void test_gvs_media(void)
{
    const uint8_t destination[6] = {0, 1, 2, 3, 4, 5};
    const uint8_t source[6] = {6, 7, 8, 9, 10, 11};
    const uint8_t payload[3] = {0xd5, 0x55, 0xd4};
    unsigned char audio[DF_GVS_AUDIO_HEADER_LEN + 3] = {0};
    unsigned char video[DF_GVS_VIDEO_HEADER_LEN + 3] = {0};
    unsigned char serialized[DF_GVS_AUDIO_HEADER_LEN + 3];
    struct df_gvs_audio_packet audio_packet;
    struct df_gvs_video_packet video_packet;
    size_t serialized_length = 0;

    header(audio);
    audio[0x1a] = 3;
    audio[0x22] = 3;
    audio[DF_GVS_AUDIO_HEADER_LEN] = 1;
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(audio, sizeof(audio),
                                              &audio_packet));
    TEST_ASSERT_INT_EQ(3, (int)audio_packet.payload_length);
    audio[0x22] = 4;
    TEST_ASSERT_INT_EQ(-1, df_gvs_parse_audio(audio, sizeof(audio),
                                               &audio_packet));
    audio[0x22] = 3;
    audio[0x1a] = 2;
    TEST_ASSERT_INT_EQ(-1, df_gvs_parse_audio(audio, sizeof(audio),
                                               &audio_packet));
    audio[0x1a] = 3;
    TEST_ASSERT_INT_EQ(-1, df_gvs_parse_audio(audio, sizeof(audio) - 1U,
                                               &audio_packet));

    header(video);
    video[0x1a] = 3;
    video[0x1e] = 1;
    video[0x20] = 1;
    video[0x22] = 3;
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_video(video, sizeof(video),
                                              &video_packet));
    TEST_ASSERT_INT_EQ(-1, df_gvs_parse_video(video, sizeof(video) - 1U,
                                               &video_packet));
    video[0x20] = 2;
    TEST_ASSERT_INT_EQ(-1, df_gvs_parse_video(video, sizeof(video),
                                               &video_packet));

    TEST_ASSERT_INT_EQ(0, df_gvs_serialize_audio(
        destination, source, 0x1234, payload, sizeof(payload),
        serialized, sizeof(serialized), &serialized_length));
    TEST_ASSERT_INT_EQ((int)sizeof(serialized), (int)serialized_length);
    TEST_ASSERT_INT_EQ(0, memcmp(serialized, gvs_magic, sizeof(gvs_magic)));
    TEST_ASSERT_INT_EQ(0, memcmp(serialized + 10, destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(serialized + 16, source, 6));
    TEST_ASSERT_INT_EQ(0x34, serialized[0x18]);
    TEST_ASSERT_INT_EQ(0x12, serialized[0x19]);
    TEST_ASSERT_INT_EQ(3, serialized[0x1a]);
    TEST_ASSERT_INT_EQ(1, serialized[0x1e]);
    TEST_ASSERT_INT_EQ(1, serialized[0x20]);
    TEST_ASSERT_INT_EQ(3, serialized[0x22]);
    TEST_ASSERT_INT_EQ(1, serialized[0x25]);
    TEST_ASSERT_INT_EQ(0, memcmp(serialized + DF_GVS_AUDIO_HEADER_LEN,
                                 payload, sizeof(payload)));
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(serialized, serialized_length,
                                              &audio_packet));
    TEST_ASSERT_INT_EQ(0x1234, audio_packet.sequence);
    TEST_ASSERT_INT_EQ(3, (int)audio_packet.field_c);
    TEST_ASSERT_INT_EQ(1, audio_packet.field_d);
    TEST_ASSERT_INT_EQ(1, audio_packet.field_e);
    TEST_ASSERT_INT_EQ(0x100, audio_packet.field_f);
    TEST_ASSERT_INT_EQ(0, memcmp(audio_packet.payload, payload,
                                 sizeof(payload)));
    TEST_ASSERT_INT_EQ(-1, df_gvs_serialize_audio(
        destination, source, 0x1234, payload, sizeof(payload),
        serialized, sizeof(serialized) - 1, &serialized_length));
}
