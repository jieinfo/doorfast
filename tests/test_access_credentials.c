#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "access_credentials.h"
#include "station_registry.h"
#include "test.h"

static struct df_station credential_station(const char *id,
    enum df_station_unlock_mode mode) {
    struct df_station station = {0};
    (void)snprintf(station.id, sizeof(station.id), "%s", id);
    station.unlock_mode = mode;
    return station;
}

void test_access_credentials_resolve_modes_and_redacted_status(void) {
    struct df_access_credentials credentials = {0};
    struct df_station inherited = credential_station("gate_main",
        DF_STATION_UNLOCK_INHERIT);
    struct df_station direct = credential_station("gate_side",
        DF_STATION_UNLOCK_DIRECT);
    struct df_station disabled = credential_station("gate_off",
        DF_STATION_UNLOCK_DISABLED);
    uint8_t material[8] = {0};
    enum df_station_unlock_mode mode;
    enum df_access_credential_source source;

    credentials.count = 1;
    (void)snprintf(credentials.items[0].station_id,
        sizeof(credentials.items[0].station_id), "%s", "gate_side");
    credentials.items[0].mode = DF_STATION_UNLOCK_DIRECT;
    credentials.items[0].material_configured = true;
    memcpy(credentials.items[0].material,
        "\x00\x11\x22\x33\x44\x55\x66\x77", 8);
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_resolve(&credentials,
        &inherited, "aabbccddeeff0011", material, &mode, &source));
    TEST_ASSERT_INT_EQ(DF_STATION_UNLOCK_INHERIT, mode);
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL, source);
    TEST_ASSERT_INT_EQ(0, memcmp(material,
        "\xaa\xbb\xcc\xdd\xee\xff\x00\x11", sizeof(material)));
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_resolve(&credentials,
        &direct, "aabbccddeeff0011", material, &mode, &source));
    TEST_ASSERT_INT_EQ(DF_STATION_UNLOCK_DIRECT, mode);
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_STATION, source);
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_populate_station_metadata(
        &credentials, &direct));
    TEST_ASSERT_INT_EQ(1, direct.unlock_material_configured);
    TEST_ASSERT_INT_EQ(0, memcmp(material,
        "\x00\x11\x22\x33\x44\x55\x66\x77", sizeof(material)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_resolve(
        &credentials, &disabled, "aabbccddeeff0011", material, &mode, &source));
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_NONE, source);
    /* UCI authority must survive a conflicting persisted direct record. */
    disabled = direct;
    disabled.unlock_mode = DF_STATION_UNLOCK_DISABLED;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_resolve(
        &credentials, &disabled, "aabbccddeeff0011", material, &mode, &source));
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_NONE, source);
    inherited = direct;
    inherited.unlock_mode = DF_STATION_UNLOCK_INHERIT;
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_resolve(&credentials,
        &inherited, "aabbccddeeff0011", material, &mode, &source));
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL, source);
    TEST_ASSERT_INT_EQ(0xaa, material[0]);
    {
        struct df_station missing = credential_station("gate_missing",
            DF_STATION_UNLOCK_DIRECT);
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_resolve(
            &credentials, &missing, "aabbccddeeff0011", material, &mode, &source));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_set(
        "/tmp/doorfast-invalid-mode", "gate_main", (enum df_station_unlock_mode)99, NULL));
}

void test_access_credentials_load_rejects_duplicate_and_malformed_records(void) {
    char path[] = "/tmp/doorfast-credentials-test-XXXXXX";
    int fd = mkstemp(path);
    struct df_access_credentials credentials = {0};
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    TEST_ASSERT_INT_EQ(0, fchmod(fd, 0600));
    TEST_ASSERT_INT_EQ((int)strlen("gate_main inherit\n"),
        (int)write(fd, "gate_main inherit\n", strlen("gate_main inherit\n")));
    close(fd);
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_load(&credentials, path));
    TEST_ASSERT_INT_EQ(1, credentials.count);
    unlink(path);
    df_access_credentials_destroy(&credentials);

    memcpy(path, "/tmp/doorfast-credentials-test-XXXXXX", sizeof(path));
    fd = mkstemp(path);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd >= 0) {
        TEST_ASSERT_INT_EQ(0, fchmod(fd, 0600));
        TEST_ASSERT_INT_EQ((int)strlen("gate_main inherit\ngate_main disabled\n"),
            (int)write(fd, "gate_main inherit\ngate_main disabled\n",
                        strlen("gate_main inherit\ngate_main disabled\n")));
        close(fd);
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_load(&credentials, path));
        unlink(path);
    }

    memcpy(path, "/tmp/doorfast-credentials-test-XXXXXX", sizeof(path));
    fd = mkstemp(path);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd >= 0) {
        TEST_ASSERT_INT_EQ(0, fchmod(fd, 0644));
        TEST_ASSERT_INT_EQ((int)strlen("gate_main inherit\n"),
            (int)write(fd, "gate_main inherit\n", strlen("gate_main inherit\n")));
        close(fd);
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_load(&credentials, path));
        unlink(path);
    }
}

void test_access_credentials_clear_is_atomic_and_publicly_redacted(void) {
    /* Actual parser I/O: malformed and oversized contents, each fresh path. */
    for (unsigned oversized = 0; oversized < 2; ++oversized) {
        char invalid_path[] = "/tmp/doorfast-credentials-invalid-XXXXXX";
        struct df_access_credentials invalid = {0};
        int invalid_fd = mkstemp(invalid_path);
        TEST_ASSERT_INT_EQ(1, invalid_fd >= 0);
        if (invalid_fd >= 0) {
            TEST_ASSERT_INT_EQ(0, fchmod(invalid_fd, 0600));
            if (oversized) TEST_ASSERT_INT_EQ(0, ftruncate(invalid_fd, 16385));
            else TEST_ASSERT_INT_EQ(17, write(invalid_fd, "gate_main direct\n", 17));
            close(invalid_fd);
            TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_access_credentials_load(&invalid, invalid_path));
            unlink(invalid_path);
        }
    }
    char path[] = "/tmp/doorfast-credentials-clear-XXXXXX";
    int fd = mkstemp(path);
    struct df_access_credentials credentials = {0};
    struct stat st;
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    close(fd);
    unlink(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_set(path,
        "gate_main", DF_STATION_UNLOCK_DIRECT, "0011223344556677"));
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_clear(path,
        "gate_main"));
    TEST_ASSERT_INT_EQ(0, stat(path, &st));
    TEST_ASSERT_INT_EQ(0600, st.st_mode & 0777);
    unlink(path);
    df_access_credentials_destroy(&credentials);
}
