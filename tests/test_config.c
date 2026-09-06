#include <string.h>

#include "config.h"
#include "test.h"

void test_config_validation(void) {
    struct df_config valid = {
        .enabled = true,
        .brand = "dnake",
        .capture_interface = "br-door",
    };
    struct df_config invalid_brand = {
        .enabled = true,
        .brand = "other",
        .capture_interface = "br-door",
    };
    struct df_config invalid_capture = {
        .enabled = true,
        .brand = "dnake",
        .capture_interface = "",
        .capture_auto = false,
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&valid));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&invalid_brand));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&invalid_capture));
}

void test_config_redaction(void) {
    char output[32];

    df_config_redact(output, sizeof(output), "abcdefghijk");
    TEST_ASSERT_INT_EQ(0, strcmp("abcd...", output));
}
