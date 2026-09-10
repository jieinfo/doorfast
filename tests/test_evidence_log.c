#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "evidence_log.h"
#include "evidence_metadata.h"
#include "test.h"

void test_evidence_log_preserves_fixed_fields_and_rotates(void) {
    char directory[] = "/tmp/doorfast-evidence-log-XXXXXX";
    char path[512], contents[2048];
    struct df_evidence_log log;
    struct df_evidence_log_status status;
    struct df_evidence_log_config config = {
        .directory = directory, .prefix = "events",
        .segment_count = 2, .segment_bytes = 700,
    };
    const struct df_evidence_log_record record = {
        .wall_seconds = 1700000000, .wall_microseconds = 123456,
        .monotonic_ms = 987654, .interface_name = "br-door",
        .source_mac = "00:11:22:33:44:55",
        .destination_mac = "66:77:88:99:aa:bb",
        .source_ip = "192.0.2.10", .destination_ip = "192.0.2.20",
        .source_port = 8300, .destination_port = 40000,
        .source_gvs = "DS:2-1-1-0", .destination_gvs = "IS:2-1-101-1",
        .family = 3, .opcode = 1, .length = 57,
        .session_generation = 9, .health_state = "recording",
    };

    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_open(&log, &config));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_append(&log, &record));
    snprintf(path, sizeof(path), "%s/events-000.partial", directory);
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    size_t length = file == NULL ? 0 : fread(contents, 1, sizeof(contents) - 1, file);
    if (file != NULL) (void)fclose(file);
    contents[length] = '\0';
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"source_mac\":\"00:11:22:33:44:55\"") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"source_ip\":\"192.0.2.10\"") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"destination_gvs\":\"IS:2-1-101-1\"") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"session_generation\":9") != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_append(&log, &record));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_append(&log, &record));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_status(&log, &status));
    TEST_ASSERT_INT_EQ(2, (int)status.completed_segments);
    TEST_ASSERT_INT_EQ(0, (int)status.active_slot);
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_close(&log));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_open(&log, &config));
    uint8_t packet[84] = {0};
    packet[12] = 8; packet[14] = 0x45; packet[17] = 70; packet[23] = 17;
    packet[26] = 192; packet[28] = 2; packet[29] = 10;
    packet[34] = 0x20; packet[35] = 0x6c; packet[39] = 50;
    memcpy(packet + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    memset(packet + 64, 0xff, 16);
    packet[80] = 3; packet[81] = 1;
    const struct df_capture_record captured = {.data = packet,
        .captured_length = sizeof(packet), .original_length = sizeof(packet)};
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_metadata_append(&log, &captured, "br-door", 42));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_status(&log, &status));
    snprintf(path, sizeof(path), "%s/events-%03u.partial", directory, status.active_slot);
    file = fopen(path, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    length = file ? fread(contents, 1, sizeof(contents) - 1, file) : 0;
    if (file) fclose(file);
    contents[length] = 0;
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"source_ip\":\"192.0.2.10\"") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "\"family\":3") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(contents, "ffffffff") == NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_log_close(&log));
    for (int slot = 0; slot < 2; ++slot) {
        struct stat metadata;
        snprintf(path, sizeof(path), "%s/events-%03d.jsonl", directory, slot);
        TEST_ASSERT_INT_EQ(0, stat(path, &metadata));
        TEST_ASSERT_INT_EQ(0600, metadata.st_mode & 0777);
        (void)unlink(path);
        snprintf(path, sizeof(path), "%s/events-%03d.partial", directory, slot);
        (void)unlink(path);
    }
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}
