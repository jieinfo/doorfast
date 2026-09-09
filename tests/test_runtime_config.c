#include <string.h>

#include "runtime_config.h"
#include "test.h"

void test_runtime_config_parses_main_gvs_section(void) {
    const char input[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'vlan-door.42'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption uplink_interface 'br-home'\n"
        "\toption passive_only '1'\n"
        "\toption capture_promiscuous '0'\n";
    struct df_runtime_config runtime;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(1, runtime.config.enabled);
    TEST_ASSERT_INT_EQ(0, strcmp("gvs", runtime.config.brand));
    TEST_ASSERT_INT_EQ(0, strcmp("vlan-door.42", runtime.config.gvs_interface));
    TEST_ASSERT_INT_EQ(0, strcmp("IS:2-1-101-1", runtime.config.gvs_local_address));
    TEST_ASSERT_INT_EQ(0, strcmp("br-home", runtime.config.uplink_interface));
    TEST_ASSERT_INT_EQ(0, strcmp("/etc/config/doorfast-sync",
                                 runtime.config.sync_state_path));
    TEST_ASSERT_INT_EQ(1, runtime.config.passive_only);
    TEST_ASSERT_INT_EQ(0, runtime.config.capture_promiscuous);
    TEST_ASSERT_INT_EQ(-1, runtime.config.unlock_delay_seconds);
    TEST_ASSERT_INT_EQ(-1, runtime.config.hangup_delay_seconds);
}

void test_runtime_config_accepts_disabled_minimal_config(void) {
    const char input[] = "config gvs 'main'\n\toption enabled '0'\n";
    struct df_runtime_config runtime;

    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_config_parse(input, &runtime));
    TEST_ASSERT_INT_EQ(0, runtime.config.enabled);
    TEST_ASSERT_INT_EQ(1, runtime.config.passive_only);
}

void test_runtime_config_rejects_ambiguous_or_unsafe_config(void) {
    const char duplicate[] =
        "config gvs 'main'\n"
        "\toption enabled '0'\n"
        "\toption enabled '1'\n";
    const char active[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption passive_only '0'\n";
    const char invalid_boolean[] =
        "config gvs 'main'\n"
        "\toption enabled 'yes'\n";
    const char relative_state[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption sync_state_path '../doorfast-sync'\n"
        "\toption passive_only '1'\n";
    const char unrelated_state[] =
        "config gvs 'main'\n"
        "\toption enabled '1'\n"
        "\toption gvs_interface 'eth9'\n"
        "\toption gvs_local_address 'IS:2-1-101-1'\n"
        "\toption sync_state_path '/etc/passwd'\n"
        "\toption passive_only '1'\n";
    struct df_runtime_config runtime;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(duplicate, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(active, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(invalid_boolean, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(relative_state, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(unrelated_state, &runtime));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse("", &runtime));
}
