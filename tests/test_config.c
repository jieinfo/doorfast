#include <string.h>

#include "config.h"
#include "test.h"

void test_config_validation(void) {
    struct df_config valid = {
        .enabled = true,
        .brand = "gvs",
        .gvs_interface = "br-door",
        .gvs_local_address = "IS:2-1-101-1",
        .passive_only = true,
    };
    struct df_config invalid_brand = {
        .enabled = true,
        .brand = "other",
        .gvs_interface = "br-door",
        .gvs_local_address = "IS:2-1-101-1",
        .passive_only = true,
    };
    struct df_config invalid_capture = {
        .enabled = true,
        .brand = "gvs",
        .gvs_interface = "",
        .gvs_local_address = "IS:2-1-101-1",
        .passive_only = true,
    };

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&valid));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&invalid_brand));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&invalid_capture));
}

void test_gvs_config_requires_explicit_passive_interface(void) {
    struct df_config valid = {
        .enabled = true,
        .brand = "gvs",
        .gvs_interface = "vlan-door.42",
        .gvs_local_address = "IS:2-1-101-1",
        .uplink_interface = "bond-home",
        .passive_only = true,
    };
    struct df_config missing_interface = valid;
    struct df_config missing_address = valid;
    struct df_config malformed_address = valid;
    struct df_config active_request = valid;

    missing_interface.gvs_interface = "";
    missing_address.gvs_local_address = "";
    malformed_address.gvs_local_address = "IS:2-1-200-0";
    active_request.passive_only = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&valid));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&missing_interface));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&missing_address));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&malformed_address));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&active_request));
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
    TEST_ASSERT_INT_EQ(0, imported.config.enabled);
    TEST_ASSERT_INT_EQ(3, imported.config.unlock_delay_seconds);
    TEST_ASSERT_INT_EQ(2, imported.config.hangup_delay_seconds);
    TEST_ASSERT_INT_EQ(1, imported.config.call_elev);
    TEST_ASSERT_INT_EQ(1, imported.config.capture_auto);
    TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&imported.config));
}
