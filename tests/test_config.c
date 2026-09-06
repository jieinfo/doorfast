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

void test_legacy_config_import_keeps_only_safe_fields(void) {
    const char legacy[] =
        "config doorlink 'settings'\n"
        "\toption brand 'dnake'\n"
        "\toption enabled '1'\n"
        "\toption auth 'never-import-this'\n"
        "\toption auto_update '1'\n"
        "config doorlink 'hass'\n"
        "\toption token 'never-import-this-either'\n"
        "config doorlink 'automation'\n"
        "\toption unlock '3'\n"
        "\toption hangup '2'\n"
        "\toption call_elev '1'\n";
    struct df_legacy_import imported;

    TEST_ASSERT_INT_EQ(DF_OK, df_config_import_legacy(legacy, &imported));
    TEST_ASSERT_INT_EQ(0, strcmp("dnake", imported.brand));
    TEST_ASSERT_INT_EQ(1, imported.config.enabled);
    TEST_ASSERT_INT_EQ(3, imported.config.unlock_delay_seconds);
    TEST_ASSERT_INT_EQ(2, imported.config.hangup_delay_seconds);
    TEST_ASSERT_INT_EQ(1, imported.config.call_elev);
    TEST_ASSERT_INT_EQ(1, imported.config.capture_auto);
    TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&imported.config));
}
