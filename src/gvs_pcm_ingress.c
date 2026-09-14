#include "gvs_pcm_ingress.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "doorfast.h"

static const uint8_t pcm_magic[DF_GVS_PCM_INGRESS_MAGIC_SIZE] = {
    'D', 'F', 'P', 'C', 'M', '0', '1', 0
};

static void put_le64(uint8_t *output, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8U; index++) {
        output[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint64_t get_le64(const uint8_t *input)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8U; index++) {
        value |= (uint64_t)input[index] << (index * 8U);
    }
    return value;
}

static int16_t get_le16_signed(const uint8_t *input)
{
    uint16_t value = (uint16_t)(input[0] | ((uint16_t)input[1] << 8U));

    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)(-(int32_t)(UINT16_MAX - value + 1U));
}

int df_gvs_pcm_ingress_serialize(uint64_t generation, const int16_t *pcm,
                                 size_t sample_count, uint8_t *output,
                                 size_t capacity, size_t *output_length)
{
    size_t index;

    if (output_length != NULL) {
        *output_length = 0;
    }
    if (generation == 0U || pcm == NULL ||
        sample_count != DF_GVS_AUDIO_TX_SAMPLES || output == NULL ||
        capacity < DF_GVS_PCM_INGRESS_PACKET_SIZE || output_length == NULL) {
        return DF_ERR_INVALID;
    }
    memcpy(output, pcm_magic, sizeof(pcm_magic));
    put_le64(output + DF_GVS_PCM_INGRESS_MAGIC_SIZE, generation);
    for (index = 0; index < sample_count; index++) {
        uint16_t sample = (uint16_t)pcm[index];
        size_t offset = DF_GVS_PCM_INGRESS_HEADER_SIZE + index * 2U;

        output[offset] = (uint8_t)sample;
        output[offset + 1U] = (uint8_t)(sample >> 8U);
    }
    *output_length = DF_GVS_PCM_INGRESS_PACKET_SIZE;
    return DF_OK;
}

int df_gvs_pcm_ingress_open(struct df_gvs_pcm_ingress *ingress,
                            const char *path)
{
    struct sockaddr_un address;
    struct stat existing;
    int flags;

    if (ingress == NULL || path == NULL || path[0] == '\0' ||
        strlen(path) >= sizeof(address.sun_path)) {
        return DF_ERR_INVALID;
    }
    memset(ingress, 0, sizeof(*ingress));
    ingress->fd = -1;
    if (lstat(path, &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || unlink(path) != 0) {
            return DF_ERR_INVALID;
        }
    } else if (errno != ENOENT) {
        return DF_ERR_IO;
    }
    ingress->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (ingress->fd < 0) {
        return DF_ERR_IO;
    }
    flags = fcntl(ingress->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(ingress->fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        df_gvs_pcm_ingress_close(ingress);
        return DF_ERR_IO;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1U);
    if (bind(ingress->fd, (const struct sockaddr *)&address,
             sizeof(address)) != 0) {
        close(ingress->fd);
        ingress->fd = -1;
        return DF_ERR_IO;
    }
    memcpy(ingress->path, path, strlen(path) + 1U);
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        df_gvs_pcm_ingress_close(ingress);
        return DF_ERR_IO;
    }
    return DF_OK;
}

int df_gvs_pcm_ingress_receive(struct df_gvs_pcm_ingress *ingress,
                               uint64_t *generation, int16_t *pcm,
                               size_t capacity)
{
    uint8_t packet[DF_GVS_PCM_INGRESS_PACKET_SIZE + 1U];
    ssize_t length;
    size_t index;

    if (ingress == NULL || ingress->fd < 0 || generation == NULL ||
        pcm == NULL || capacity < DF_GVS_AUDIO_TX_SAMPLES) {
        return DF_GVS_PCM_INGRESS_ERROR;
    }
    *generation = 0;
    length = recv(ingress->fd, packet, sizeof(packet), MSG_DONTWAIT);
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return DF_GVS_PCM_INGRESS_EMPTY;
    }
    if (length < 0) {
        return DF_GVS_PCM_INGRESS_ERROR;
    }
    if ((size_t)length != DF_GVS_PCM_INGRESS_PACKET_SIZE ||
        memcmp(packet, pcm_magic, sizeof(pcm_magic)) != 0) {
        return DF_GVS_PCM_INGRESS_INVALID;
    }
    *generation = get_le64(packet + DF_GVS_PCM_INGRESS_MAGIC_SIZE);
    if (*generation == 0U) {
        return DF_GVS_PCM_INGRESS_INVALID;
    }
    for (index = 0; index < DF_GVS_AUDIO_TX_SAMPLES; index++) {
        size_t offset = DF_GVS_PCM_INGRESS_HEADER_SIZE + index * 2U;
        pcm[index] = get_le16_signed(packet + offset);
    }
    return DF_GVS_PCM_INGRESS_FRAME;
}

void df_gvs_pcm_ingress_close(struct df_gvs_pcm_ingress *ingress)
{
    if (ingress == NULL) {
        return;
    }
    if (ingress->fd >= 0) {
        close(ingress->fd);
    }
    if (ingress->path[0] != '\0') {
        unlink(ingress->path);
    }
    memset(ingress, 0, sizeof(*ingress));
    ingress->fd = -1;
}
