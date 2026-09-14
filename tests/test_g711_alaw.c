#include "test.h"
#include "g711_alaw.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void test_g711_alaw(void)
{
    const uint8_t encoded[] = {0xd5, 0x55};
    const int16_t source[] = {0, -1, 8, -8};
    const uint8_t expected[] = {0xd5, 0x55, 0xd5, 0x55};
    uint8_t encoded_source[4];
    int16_t pcm[2];
    char path[128];
    uint8_t header[44];
    FILE *file;

    TEST_ASSERT_INT_EQ(8, df_g711_alaw_decode_sample(0xd5));
    TEST_ASSERT_INT_EQ(-8, df_g711_alaw_decode_sample(0x55));
    TEST_ASSERT_INT_EQ(0xd5, df_g711_alaw_encode_sample(0));
    TEST_ASSERT_INT_EQ(0x55, df_g711_alaw_encode_sample(-1));
    TEST_ASSERT_INT_EQ(0, df_g711_alaw_encode(source, 4,
                                               encoded_source, 4));
    TEST_ASSERT_INT_EQ(0, memcmp(expected, encoded_source,
                                 sizeof(expected)));
    TEST_ASSERT_INT_EQ(-1, df_g711_alaw_encode(source, 4,
                                                encoded_source, 3));
    TEST_ASSERT_INT_EQ(0, df_g711_alaw_decode(encoded, 2, pcm, 2));
    TEST_ASSERT_INT_EQ(8, pcm[0]);
    TEST_ASSERT_INT_EQ(-8, pcm[1]);

    (void)snprintf(path, sizeof(path), "/tmp/doorfast-audio-%ld.wav",
                   (long)getpid());
    TEST_ASSERT_INT_EQ(0, df_g711_alaw_write_wav(path, encoded, 2));
    file = fopen(path, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) {
        TEST_ASSERT_INT_EQ(44, (int)fread(header, 1, 44, file));
        TEST_ASSERT_INT_EQ(0, memcmp(header, "RIFF", 4));
        TEST_ASSERT_INT_EQ(0, memcmp(header + 8, "WAVE", 4));
        TEST_ASSERT_INT_EQ(4, header[40]);
        fclose(file);
    }
    unlink(path);
}
