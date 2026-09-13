#include "test.h"

#include <string.h>

#include "gvs_vendor_header.h"

static int fixed_random(uint8_t *output, size_t length, void *context) {
    const uint8_t *input = context;

    if (output == NULL || input == NULL || length != DF_GVS_HEADER_FIELD_SIZE)
        return DF_ERR_INVALID;
    memcpy(output, input, length);
    return DF_OK;
}

static int failed_random(uint8_t *output, size_t length, void *context) {
    (void)output;
    (void)length;
    (void)context;
    return DF_ERR_IO;
}

void test_gvs_udp_transform_matches_vendor_vectors(void) {
    static const uint8_t inputs[][DF_GVS_HEADER_FIELD_SIZE] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
        {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38},
    };
    static const uint8_t expected[][DF_GVS_HEADER_FIELD_SIZE] = {
        {0x00, 0x42, 0x42, 0x41, 0x26, 0x53, 0x56, 0x47},
        {0x00, 0x40, 0x46, 0x47, 0x2e, 0x59, 0x5a, 0x49},
        {0xfe, 0xbc, 0xbc, 0xbf, 0xd8, 0xad, 0xa8, 0xb9},
        {0x62, 0x26, 0x24, 0x29, 0x4c, 0x3f, 0x38, 0x37},
    };
    uint8_t output[DF_GVS_HEADER_FIELD_SIZE];
    size_t index;

    for (index = 0; index < sizeof(inputs) / sizeof(inputs[0]); index++) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_transform(inputs[index], output));
        TEST_ASSERT_INT_EQ(0, memcmp(output, expected[index], sizeof(output)));
    }
}

void test_gvs_vendor_header_serializes_random_and_transformed_fields(void) {
    const uint8_t source[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t destination[6] = {0x32, 0x02, 0x01, 0x00, 0x01, 0x00};
    const uint8_t random[8] = {0x00, 0x01, 0x02, 0x03,
                               0x04, 0x05, 0x06, 0x07};
    const uint8_t transformed[8] = {0x00, 0x40, 0x46, 0x47,
                                    0x2e, 0x59, 0x5a, 0x49};
    const struct df_gvs_vendor_header_context context = {
        .fill_random = fixed_random,
        .random_context = (void *)random,
    };
    uint8_t frame[DF_GVS_CONTROL_HEADER_SIZE];
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(
        frame, sizeof(frame), &length, destination, source, 0x08, 0x03,
        NULL, 0, df_gvs_vendor_header_fields, (void *)&context));
    TEST_ASSERT_INT_EQ(DF_GVS_CONTROL_HEADER_SIZE, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(frame + 22, random, sizeof(random)));
    TEST_ASSERT_INT_EQ(0, memcmp(frame + 30, transformed, sizeof(transformed)));
}

void test_gvs_vendor_header_clears_fields_when_random_source_fails(void) {
    const uint8_t source[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t destination[6] = {0x32, 0x02, 0x01, 0x00, 0x01, 0x00};
    const struct df_gvs_header_request request = {
        .destination = destination,
        .source = source,
        .family = 0x03,
        .opcode = 0x02,
    };
    const struct df_gvs_vendor_header_context context = {
        .fill_random = failed_random,
    };
    uint8_t random[DF_GVS_HEADER_FIELD_SIZE];
    uint8_t transformed[DF_GVS_HEADER_FIELD_SIZE];
    uint8_t zero[DF_GVS_HEADER_FIELD_SIZE] = {0};

    memset(random, 0xa5, sizeof(random));
    memset(transformed, 0x5a, sizeof(transformed));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_vendor_header_fields(
        &request, random, transformed, (void *)&context));
    TEST_ASSERT_INT_EQ(0, memcmp(random, zero, sizeof(random)));
    TEST_ASSERT_INT_EQ(0, memcmp(transformed, zero, sizeof(transformed)));
}

void test_gvs_vendor_header_reads_system_random(void) {
    const uint8_t source[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t destination[6] = {0x32, 0x02, 0x01, 0x00, 0x01, 0x00};
    const struct df_gvs_header_request request = {
        .destination = destination,
        .source = source,
        .family = 0x03,
        .opcode = 0x81,
    };
    uint8_t random[DF_GVS_HEADER_FIELD_SIZE];
    uint8_t transformed[DF_GVS_HEADER_FIELD_SIZE];
    uint8_t expected[DF_GVS_HEADER_FIELD_SIZE];

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_vendor_header_fields(
        &request, random, transformed, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_transform(random, expected));
    TEST_ASSERT_INT_EQ(0, memcmp(transformed, expected, sizeof(expected)));
}
