#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "deployment_config.h"
#include "test.h"

static int read_fixture(char *output, size_t output_size) {
    FILE *file;
    size_t length;

    file = fopen("tests/fixtures/doorfast-deployment-valid.conf", "rb");
    if (file == NULL) {
        return -1;
    }
    length = fread(output, 1, output_size - 1, file);
    if (ferror(file) || !feof(file)) {
        fclose(file);
        return -1;
    }
    output[length] = '\0';
    return fclose(file);
}

void test_deployment_config_parses_valid_profile(void) {
    char input[1024];
    struct df_deployment_config config;

    TEST_ASSERT_INT_EQ(0, read_fixture(input, sizeof(input)));
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_config_parse(input, &config));
    TEST_ASSERT_INT_EQ(1, config.enabled);
    TEST_ASSERT_INT_EQ(0, config.recording_enabled);
    TEST_ASSERT_INT_EQ(0, strcmp("br-door", config.bridge));
    TEST_ASSERT_INT_EQ(0, strcmp("door-up", config.upstream));
    TEST_ASSERT_INT_EQ(0, strcmp("door-down", config.downstream));
    TEST_ASSERT_INT_EQ(0, strcmp("br-lan", config.management));
    TEST_ASSERT_INT_EQ(0, strcmp("/mnt/doorfast", config.evidence_root));
    TEST_ASSERT_INT_EQ(14336, (int)config.recent_budget_mib);
    TEST_ASSERT_INT_EQ(8192, (int)config.control_budget_mib);
    TEST_ASSERT_INT_EQ(1024, (int)config.log_budget_mib);
    TEST_ASSERT_INT_EQ(6144, (int)config.reserve_mib);
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_config_validate(&config));
}

void test_deployment_config_rejects_unsafe_profile(void) {
    const char duplicate_interface[] =
        "config inline 'main'\n"
        " option enabled '1'\n"
        " option bridge 'br-door'\n"
        " option upstream 'door-up'\n"
        " option downstream 'door-down'\n"
        " option management 'door-down'\n";
    const char wrong_root[] =
        "config inline 'main'\n option enabled '0'\n"
        " option evidence_root '/tmp/doorfast'\n";
    const char oversized_budget[] =
        "config inline 'main'\n option enabled '0'\n"
        " option recent_budget_mib '14337'\n";
    const char low_reserve[] =
        "config inline 'main'\n option enabled '0'\n"
        " option reserve_mib '6143'\n";
    const char active_empty_interface[] =
        "config inline 'main'\n option enabled '1'\n";
    const char recording_without_deployment[] =
        "config inline 'main'\n option enabled '0'\n"
        " option recording_enabled '1'\n";
    const char disabled_minimal[] =
        "config inline 'main'\n option enabled '0'\n";
    struct df_deployment_config config;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(duplicate_interface, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(wrong_root, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(oversized_budget, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(low_reserve, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(active_empty_interface, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(recording_without_deployment, &config));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_deployment_config_parse(disabled_minimal, &config));
    TEST_ASSERT_INT_EQ(0, config.enabled);
}

void test_deployment_config_rejects_ambiguous_input(void) {
    const char duplicate_option[] =
        "config inline 'main'\n"
        " option enabled '0'\n"
        " option enabled '1'\n";
    const char malformed_integer[] =
        "config inline 'main'\n option enabled '0'\n"
        " option recent_budget_mib '12MiB'\n";
    const char overflowing_integer[] =
        "config inline 'main'\n option enabled '0'\n"
        " option recent_budget_mib '4294967296'\n";
    const char unknown_option[] =
        "config inline 'main'\n option enabled '0'\n"
        " option surprise '1'\n";
    const char duplicate_main[] =
        "config inline 'main'\n option enabled '0'\n"
        "config inline 'main'\n option enabled '0'\n";
    struct df_deployment_config config;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(duplicate_option, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(malformed_integer, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(overflowing_integer, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(unknown_option, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(duplicate_main, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse(NULL, &config));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_deployment_config_parse("", &config));
    const char *bad_values[] = {"-1", "+1", " 6144", "6144 ", "", "18446744073709551616"};
    memset(&config, 0x5a, sizeof(config));
    struct df_deployment_config before = config;
    for (size_t i = 0; i < sizeof(bad_values) / sizeof(bad_values[0]); ++i) {
        char input[256];
        snprintf(input, sizeof(input), "config inline 'main'\n option reserve_mib '%s'\n", bad_values[i]);
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_parse(input, &config));
        TEST_ASSERT_INT_EQ(0, memcmp(&before, &config, sizeof(config)));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_validate(NULL));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_parse("config inline 'main'", NULL));
    char input[1024];
    TEST_ASSERT_INT_EQ(0, read_fixture(input, sizeof(input)));
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_config_parse(input, &config));
    before = config;
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < i; ++j) {
            config = before;
            char *names[] = {config.bridge, config.upstream, config.downstream, config.management};
            strcpy(names[i], names[j]);
            TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_validate(&config));
        }
    }
    config = before;
    config.recent_budget_mib = UINT32_MAX;
    config.control_budget_mib = UINT32_MAX;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_validate(&config));
    config = before;
    memset(config.bridge, 'a', sizeof(config.bridge));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_validate(&config));
    config = before;
    strcpy(config.upstream, "../eth0");
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_deployment_config_validate(&config));
}
