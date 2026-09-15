#include "runtime_id.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int df_runtime_id_system_fill(
    uint8_t *output, size_t length, void *context) {
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
        return DF_ERR_IO;
    }
    return close(fd) == 0 ? DF_OK : DF_ERR_IO;
}

int df_runtime_id_generate(char output[DF_RUNTIME_ID_HEX_LENGTH + 1U],
    df_runtime_id_fill_fn fill, void *context) {
    static const char digits[] = "0123456789abcdef";
    uint8_t bytes[DF_RUNTIME_ID_HEX_LENGTH / 2U];
    size_t index;

    if (output == NULL)
        return DF_ERR_INVALID;
    memset(output, 0, DF_RUNTIME_ID_HEX_LENGTH + 1U);
    if (fill == NULL)
        fill = df_runtime_id_system_fill;
    if (fill(bytes, sizeof(bytes), context) != DF_OK)
        return DF_ERR_IO;
    for (index = 0; index < sizeof(bytes); index++) {
        output[index * 2U] = digits[bytes[index] >> 4U];
        output[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return DF_OK;
}

bool df_runtime_id_is_valid(const char *value) {
    size_t index;

    if (value == NULL)
        return false;
    for (index = 0; index < DF_RUNTIME_ID_HEX_LENGTH; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f')))
            return false;
    }
    return value[DF_RUNTIME_ID_HEX_LENGTH] == '\0';
}
