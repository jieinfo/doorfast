#include <arpa/inet.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gvs_media_receiver.h"
#include "test.h"

static int send_loopback(uint16_t port, const uint8_t *data, size_t length) {
    struct sockaddr_in destination = {0};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ssize_t sent;

    if (fd < 0) return -1;
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sent = sendto(fd, data, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    (void)close(fd);
    return sent == (ssize_t)length ? 0 : -1;
}

static int receive_with_retry(struct df_gvs_media_receiver *receiver,
    uint8_t *buffer, size_t capacity,
    struct df_gvs_media_datagram *datagram) {
    unsigned attempt;

    for (attempt = 0U; attempt < 1000U; attempt++) {
        int result = df_gvs_media_receiver_next(receiver, buffer, capacity,
            datagram);

        if (result != DF_GVS_MEDIA_RECEIVER_EMPTY) return result;
    }
    return DF_GVS_MEDIA_RECEIVER_EMPTY;
}

void test_gvs_media_receiver_binds_and_routes_complete_datagrams(void) {
    static const uint8_t audio[] = {0x47, 0x56, 0x53, 0x02};
    static const uint8_t video[] = {0x47, 0x56, 0x53, 0x03, 0xff, 0xd8};
    struct df_gvs_media_receiver receiver = {
        .audio_fd = -1,
        .video_fd = -1,
    };
    struct df_gvs_media_datagram datagram;
    uint8_t buffer[32];
    bool saw_audio = false;
    bool saw_video = false;
    unsigned index;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_receiver_open(
        &receiver, "127.0.0.1", 0U, 0U));
    TEST_ASSERT_INT_EQ(1, receiver.audio_fd >= 0);
    TEST_ASSERT_INT_EQ(1, receiver.video_fd >= 0);
    TEST_ASSERT_INT_EQ(1, receiver.audio_port != 0U);
    TEST_ASSERT_INT_EQ(1, receiver.video_port != 0U);
    TEST_ASSERT_INT_EQ(1, receiver.audio_port != receiver.video_port);
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_RECEIVER_EMPTY,
        df_gvs_media_receiver_next(&receiver, buffer, sizeof(buffer),
            &datagram));

    TEST_ASSERT_INT_EQ(0, send_loopback(receiver.audio_port,
        audio, sizeof(audio)));
    TEST_ASSERT_INT_EQ(0, send_loopback(receiver.video_port,
        video, sizeof(video)));
    for (index = 0U; index < 2U; index++) {
        TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_RECEIVER_DATAGRAM,
            receive_with_retry(&receiver, buffer, sizeof(buffer), &datagram));
        TEST_ASSERT_INT_EQ((int)htonl(INADDR_LOOPBACK),
            (int)datagram.source_ipv4);
        if (datagram.channel == DF_GVS_MEDIA_AUDIO) {
            saw_audio = true;
            TEST_ASSERT_INT_EQ((int)sizeof(audio), (int)datagram.length);
            TEST_ASSERT_INT_EQ(0, memcmp(audio, buffer, sizeof(audio)));
        } else if (datagram.channel == DF_GVS_MEDIA_VIDEO) {
            saw_video = true;
            TEST_ASSERT_INT_EQ((int)sizeof(video), (int)datagram.length);
            TEST_ASSERT_INT_EQ(0, memcmp(video, buffer, sizeof(video)));
        }
    }
    TEST_ASSERT_INT_EQ(1, saw_audio ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, saw_video ? 1 : 0);
    TEST_ASSERT_INT_EQ(1, (int)receiver.audio_received);
    TEST_ASSERT_INT_EQ(1, (int)receiver.video_received);
    df_gvs_media_receiver_close(&receiver);
    TEST_ASSERT_INT_EQ(-1, receiver.audio_fd);
    TEST_ASSERT_INT_EQ(-1, receiver.video_fd);
}

void test_gvs_media_receiver_rejects_truncation_and_duplicate_ownership(void) {
    struct df_gvs_media_receiver receiver = {
        .audio_fd = -1,
        .video_fd = -1,
    };
    struct df_gvs_media_receiver duplicate = {
        .audio_fd = -1,
        .video_fd = -1,
    };
    struct df_gvs_media_datagram datagram;
    uint8_t oversized[128];
    uint8_t buffer[32];

    memset(oversized, 0x5a, sizeof(oversized));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_receiver_open(
        &receiver, "127.0.0.1", 0U, 0U));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_media_receiver_open(&duplicate,
        "127.0.0.1", receiver.audio_port, receiver.video_port));
    TEST_ASSERT_INT_EQ(-1, duplicate.audio_fd);
    TEST_ASSERT_INT_EQ(-1, duplicate.video_fd);

    TEST_ASSERT_INT_EQ(0, send_loopback(receiver.video_port,
        oversized, sizeof(oversized)));
    TEST_ASSERT_INT_EQ(DF_GVS_MEDIA_RECEIVER_DROPPED,
        receive_with_retry(&receiver, buffer, sizeof(buffer), &datagram));
    TEST_ASSERT_INT_EQ(1, (int)receiver.truncated);
    TEST_ASSERT_INT_EQ(0, (int)receiver.video_received);
    df_gvs_media_receiver_close(&receiver);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_media_receiver_open(&duplicate,
        "127.0.0.1", receiver.audio_port, receiver.video_port));
    df_gvs_media_receiver_close(&duplicate);
}
