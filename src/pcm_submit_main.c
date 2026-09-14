#ifdef DF_PCM_SUBMIT_PROGRAM

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gvs_pcm_ingress.h"

static int parse_generation(const char *text, uint64_t *generation)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || text[0] == '\0' || text[0] == '-' ||
        text[0] == '+' || generation == NULL) {
        return -1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0U) {
        return -1;
    }
#if ULLONG_MAX > UINT64_MAX
    if (value > UINT64_MAX) {
        return -1;
    }
#endif
    *generation = (uint64_t)value;
    return 0;
}

static int16_t read_le16_signed(const uint8_t input[2])
{
    uint16_t value = (uint16_t)(input[0] | ((uint16_t)input[1] << 8U));

    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)(-(int32_t)(UINT16_MAX - value + 1U));
}

int main(int argc, char **argv)
{
    uint8_t input[DF_GVS_AUDIO_TX_SAMPLES * 2U + 1U];
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES];
    const char *path = DF_GVS_PCM_INGRESS_DEFAULT_PATH;
    uint64_t generation = 0;
    size_t length;
    size_t index;

    if ((argc != 2 && argc != 3) ||
        parse_generation(argv[1], &generation) != 0) {
        (void)fprintf(stderr,
            "usage: doorfast-pcm-submit GENERATION [SOCKET]\n");
        return 2;
    }
    if (argc == 3) {
        path = argv[2];
    }
    length = fread(input, 1, sizeof(input), stdin);
    if (ferror(stdin) || length != DF_GVS_AUDIO_TX_SAMPLES * 2U) {
        (void)fprintf(stderr,
            "doorfast-pcm-submit: expected exactly 320 PCM bytes\n");
        return 2;
    }
    for (index = 0; index < DF_GVS_AUDIO_TX_SAMPLES; index++) {
        pcm[index] = read_le16_signed(input + index * 2U);
    }
    if (df_gvs_pcm_ingress_send(path, generation, pcm,
            DF_GVS_AUDIO_TX_SAMPLES) != DF_OK) {
        (void)fprintf(stderr,
            "doorfast-pcm-submit: audio ingress unavailable\n");
        return 3;
    }
    return 0;
}

#else

typedef int df_pcm_submit_program_disabled;

#endif
