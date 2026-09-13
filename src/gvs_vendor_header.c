#include "gvs_vendor_header.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int df_gvs_system_random(uint8_t *output, size_t length, void *context) {
    size_t offset = 0;
    int fd;

    (void)context;
    if (output == NULL || length == 0U)
        return DF_ERR_INVALID;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return DF_ERR_IO;
    while (offset < length) {
        ssize_t count = read(fd, output + offset, length - offset);

        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        (void)close(fd);
        memset(output, 0, length);
        return DF_ERR_IO;
    }
    if (close(fd) != 0) {
        memset(output, 0, length);
        return DF_ERR_IO;
    }
    return DF_OK;
}

int df_gvs_udp_transform(
    const uint8_t input[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t output[DF_GVS_HEADER_FIELD_SIZE]) {
    static const uint8_t mask[DF_GVS_HEADER_FIELD_SIZE] = {
        0x00, 0x42, 0x42, 0x41, 0x26, 0x53, 0x56, 0x47,
    };
    size_t index;

    if (input == NULL || output == NULL)
        return DF_ERR_INVALID;
    for (index = 0; index < DF_GVS_HEADER_FIELD_SIZE; index++)
        output[index] = (uint8_t)(((uint16_t)input[index] << 1U) ^ mask[index]);
    return DF_OK;
}

int df_gvs_vendor_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    const struct df_gvs_vendor_header_context *provider = context;
    df_gvs_random_fill_fn fill_random = df_gvs_system_random;
    void *random_context = NULL;
    uint8_t next_random[DF_GVS_HEADER_FIELD_SIZE] = {0};
    uint8_t next_encryption[DF_GVS_HEADER_FIELD_SIZE] = {0};

    if (request == NULL || request->destination == NULL ||
        request->source == NULL || random_code == NULL ||
        encryption_code == NULL ||
        (request->payload_length > 0U && request->payload == NULL))
        return DF_ERR_INVALID;
    if (provider != NULL) {
        if (provider->fill_random == NULL)
            return DF_ERR_INVALID;
        fill_random = provider->fill_random;
        random_context = provider->random_context;
    }
    if (fill_random(next_random, sizeof(next_random), random_context) != DF_OK ||
        df_gvs_udp_transform(next_random, next_encryption) != DF_OK) {
        memset(random_code, 0, DF_GVS_HEADER_FIELD_SIZE);
        memset(encryption_code, 0, DF_GVS_HEADER_FIELD_SIZE);
        return DF_ERR_IO;
    }
    memcpy(random_code, next_random, sizeof(next_random));
    memcpy(encryption_code, next_encryption, sizeof(next_encryption));
    return DF_OK;
}
