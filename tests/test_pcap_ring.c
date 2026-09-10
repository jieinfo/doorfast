#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pcap_ring.h"
#include "test.h"

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void test_pcap_ring_rotates_and_recovers_oldest_slot(void) {
    char directory[] = "/tmp/doorfast-pcap-ring-XXXXXX";
    char path[512];
    uint8_t packet[60];
    uint8_t pcap[128];
    struct df_capture_record record = {
        .data = packet,
        .captured_length = sizeof(packet),
        .original_length = 100,
        .wall_seconds = 123,
        .wall_microseconds = 456,
    };
    struct df_pcap_ring ring;
    struct df_pcap_ring_status status;
    struct df_pcap_ring_config config = {
        .directory = directory,
        .prefix = "recent",
        .segment_count = 3,
        .segment_bytes = 140,
        .snaplen = 60,
    };

    memset(packet, 0xa5, sizeof(packet));
    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_init(&ring, &config));
    for (int i = 0; i < 4; ++i)
        TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_write(&ring, &record));
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_status(&ring, &status));
    TEST_ASSERT_INT_EQ(0, (int)status.active_slot);
    TEST_ASSERT_INT_EQ(3, (int)status.completed_segments);
    for (int i = 0; i < 3; ++i) {
        snprintf(path, sizeof(path), "%s/recent-%03d.pcap", directory, i);
        TEST_ASSERT_INT_EQ(0, access(path, F_OK));
    }
    snprintf(path, sizeof(path), "%s/recent-000.partial", directory);
    TEST_ASSERT_INT_EQ(0, access(path, F_OK));
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_close(&ring));
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));

    snprintf(path, sizeof(path), "%s/recent-000.pcap", directory);
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    size_t length = file == NULL ? 0 : fread(pcap, 1, sizeof(pcap), file);
    if (file != NULL) (void)fclose(file);
    TEST_ASSERT_INT_EQ(100, (int)length);
    TEST_ASSERT_INT_EQ(0xa1b2c3d4U == read_le32(pcap), 1);
    TEST_ASSERT_INT_EQ(60, (int)read_le32(pcap + 32));
    TEST_ASSERT_INT_EQ(100, (int)read_le32(pcap + 36));

    for (int i = 0; i < 3; ++i) {
        struct timespec times[2] = {{.tv_sec = 10 + i}, {.tv_sec = 300 - i * 100}};
        snprintf(path, sizeof(path), "%s/recent-%03d.pcap", directory, i);
        TEST_ASSERT_INT_EQ(0, utimensat(AT_FDCWD, path, times, 0));
    }
    snprintf(path, sizeof(path), "%s/recent-002.partial", directory);
    file = fopen(path, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) {
        (void)fputs("truncated", file);
        (void)fclose(file);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_init(&ring, &config));
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_status(&ring, &status));
    TEST_ASSERT_INT_EQ(2, (int)status.active_slot);
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_close(&ring));

    snprintf(path, sizeof(path), "%s/link", directory);
    TEST_ASSERT_INT_EQ(0, symlink(directory, path));
    struct df_pcap_ring_config unsafe = config;
    unsafe.directory = path;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_pcap_ring_init(&ring, &unsafe));
    (void)unlink(path);
    for (int i = 0; i < 3; ++i) {
        snprintf(path, sizeof(path), "%s/recent-%03d.pcap", directory, i);
        (void)unlink(path);
        snprintf(path, sizeof(path), "%s/recent-%03d.partial", directory, i);
        (void)unlink(path);
    }
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}
