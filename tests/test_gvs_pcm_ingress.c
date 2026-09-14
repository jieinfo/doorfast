#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "gvs_pcm_ingress.h"
#include "test.h"

static int send_local_datagram(const char *path, const uint8_t *packet,
                               size_t length)
{
    struct sockaddr_un address;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ssize_t written;

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1U);
    written = sendto(fd, packet, length, 0,
        (const struct sockaddr *)&address, sizeof(address));
    close(fd);
    return written == (ssize_t)length ? 0 : -1;
}

void test_gvs_pcm_ingress_round_trips_private_local_frame(void)
{
    struct df_gvs_pcm_ingress ingress = {.fd = -1};
    int16_t source[DF_GVS_AUDIO_TX_SAMPLES] = {0};
    int16_t received[DF_GVS_AUDIO_TX_SAMPLES] = {0};
    uint8_t packet[DF_GVS_PCM_INGRESS_PACKET_SIZE];
    char path[96];
    struct stat state;
    size_t length = 0;
    uint64_t generation = 0;

    source[0] = INT16_MIN;
    source[1] = -1;
    source[2] = 0;
    source[3] = INT16_MAX;
    (void)snprintf(path, sizeof(path), "/tmp/doorfast-pcm-%ld.sock",
                   (long)getpid());
    unlink(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_ingress_open(&ingress, path));
    TEST_ASSERT_INT_EQ(0, stat(path, &state));
    TEST_ASSERT_INT_EQ(1, S_ISSOCK(state.st_mode));
    TEST_ASSERT_INT_EQ(0600, state.st_mode & 0777);
    TEST_ASSERT_INT_EQ(DF_GVS_PCM_INGRESS_EMPTY,
        df_gvs_pcm_ingress_receive(&ingress, &generation, received,
                                   DF_GVS_AUDIO_TX_SAMPLES));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_ingress_serialize(
        7, source, DF_GVS_AUDIO_TX_SAMPLES, packet, sizeof(packet), &length));
    TEST_ASSERT_INT_EQ(336, (int)length);
    TEST_ASSERT_INT_EQ(0, send_local_datagram(path, packet, length));
    TEST_ASSERT_INT_EQ(DF_GVS_PCM_INGRESS_FRAME,
        df_gvs_pcm_ingress_receive(&ingress, &generation, received,
                                   DF_GVS_AUDIO_TX_SAMPLES));
    TEST_ASSERT_INT_EQ(7, (int)generation);
    TEST_ASSERT_INT_EQ(INT16_MIN, received[0]);
    TEST_ASSERT_INT_EQ(-1, received[1]);
    TEST_ASSERT_INT_EQ(0, received[2]);
    TEST_ASSERT_INT_EQ(INT16_MAX, received[3]);
    df_gvs_pcm_ingress_close(&ingress);
    TEST_ASSERT_INT_EQ(-1, stat(path, &state));
}

void test_gvs_pcm_ingress_rejects_stale_shape_and_regular_path(void)
{
    struct df_gvs_pcm_ingress ingress = {.fd = -1};
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES] = {0};
    uint8_t packet[DF_GVS_PCM_INGRESS_PACKET_SIZE] = {0};
    char path[96];
    FILE *file;
    uint64_t generation = 99;

    (void)snprintf(path, sizeof(path), "/tmp/doorfast-pcm-invalid-%ld.sock",
                   (long)getpid());
    unlink(path);
    file = fopen(path, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) {
        fclose(file);
    }
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_pcm_ingress_open(&ingress, path));
    unlink(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_ingress_open(&ingress, path));
    TEST_ASSERT_INT_EQ(0, send_local_datagram(path, packet, sizeof(packet)));
    TEST_ASSERT_INT_EQ(DF_GVS_PCM_INGRESS_INVALID,
        df_gvs_pcm_ingress_receive(&ingress, &generation, pcm,
                                   DF_GVS_AUDIO_TX_SAMPLES));
    TEST_ASSERT_INT_EQ(0, (int)generation);
    TEST_ASSERT_INT_EQ(0, send_local_datagram(path, packet, 8));
    TEST_ASSERT_INT_EQ(DF_GVS_PCM_INGRESS_INVALID,
        df_gvs_pcm_ingress_receive(&ingress, &generation, pcm,
                                   DF_GVS_AUDIO_TX_SAMPLES));
    df_gvs_pcm_ingress_close(&ingress);
}
