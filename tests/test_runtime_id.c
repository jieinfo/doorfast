#include "test.h"

#include <string.h>

#include "runtime_id.h"

static int fixed_fill(uint8_t *output, size_t length, void *context) {
    const uint8_t *input = context;

    if (output == NULL || input == NULL || length != 8U)
        return DF_ERR_INVALID;
    memcpy(output, input, length);
    return DF_OK;
}

static int failed_fill(uint8_t *output, size_t length, void *context) {
    (void)output;
    (void)length;
    (void)context;
    return DF_ERR_IO;
}

static int short_fill(uint8_t *output, size_t length, void *context) {
    const uint8_t *input = context;

    if (output == NULL || input == NULL || length != 8U)
        return DF_ERR_INVALID;
    memcpy(output, input, length - 1U);
    return (int)(length - 1U);
}

void test_runtime_id_encodes_random_bytes_and_validates_exact_lowercase_hex(void) {
    static const uint8_t bytes[8] = {
        0x00, 0x12, 0xab, 0xff, 0x80, 0x7e, 0x55, 0x09,
    };
    char output[DF_RUNTIME_ID_HEX_LENGTH + 1U];

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_id_generate(
        output, fixed_fill, (void *)bytes));
    TEST_ASSERT_INT_EQ(0, strcmp("0012abff807e5509", output));
    TEST_ASSERT_INT_EQ(1, df_runtime_id_is_valid(output) ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_runtime_id_is_valid("0012ABff807e5509") ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_runtime_id_is_valid("0012abff807e550") ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_runtime_id_is_valid("0012abff807e55090") ? 1 : 0);
    TEST_ASSERT_INT_EQ(0, df_runtime_id_is_valid("0012abfg807e5509") ? 1 : 0);
}

void test_runtime_id_clears_output_when_random_fill_fails_or_is_short(void) {
    static const uint8_t bytes[8] = {0};
    char output[DF_RUNTIME_ID_HEX_LENGTH + 1U];

    memset(output, 0xa5, sizeof(output));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_id_generate(
        output, failed_fill, NULL));
    TEST_ASSERT_INT_EQ(0, output[0]);
    memset(output, 0xa5, sizeof(output));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_id_generate(
        output, short_fill, (void *)bytes));
    TEST_ASSERT_INT_EQ(0, output[0]);
}
