#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "evidence_recorder.h"
#include "test.h"

static int space(void *context, uint64_t *available) {
    *available = *(uint64_t *)context;
    return DF_OK;
}

void test_evidence_recorder_guards_and_classifies(void) {
    char directory[] = "/tmp/doorfast-recorder-core-XXXXXX", path[512];
    struct df_pcap_ring recent, control;
    struct df_pcap_ring_status before, after;
    struct df_evidence_recorder recorder;
    uint64_t available = 10000;
    uint8_t packet[342] = {0};
    struct df_capture_record record = {.data = packet,
        .captured_length = 84, .original_length = 84, .wall_seconds = 1};
    struct df_pcap_ring_config config = {.directory = directory,
        .prefix = "recent", .segment_count = 2, .segment_bytes = 4096,
        .snaplen = 256};
    TEST_ASSERT_INT_EQ(1, mkdtemp(directory) != NULL);
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_init(&recent, &config));
    config.prefix = "control"; config.snaplen = 2048;
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_init(&control, &config));
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_recorder_init(&recorder,
        &recent, &control, 6000, space, &available));
    packet[12] = 8; packet[14] = 0x45; packet[17] = 70; packet[23] = 17;
    packet[34] = 0x20; packet[35] = 0x6c;
    packet[39] = 50;
    memcpy(packet + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    packet[80] = 3; packet[81] = 1;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_recorder_accept(&recorder, &record));
    TEST_ASSERT_INT_EQ(1, (int)recorder.control_packets);
    packet[35] = 0x6f; packet[16] = 1; packet[17] = 72;
    packet[38] = 1; packet[39] = 52;
    record.captured_length = record.original_length = sizeof(packet);
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_recorder_accept(&recorder, &record));
    TEST_ASSERT_INT_EQ(2, (int)recorder.recent_packets);
    TEST_ASSERT_INT_EQ(1, (int)recorder.control_packets);
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_status(&recent, &before));
    TEST_ASSERT_INT_EQ(396, (int)before.bytes_written);
    available = 6000;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_recorder_accept(&recorder, &record));
    TEST_ASSERT_INT_EQ(DF_RECORDER_SPACE_GUARD, recorder.state);
    available = 10000;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_recorder_accept(&recorder, &record));
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_status(&recent, &after));
    TEST_ASSERT_INT_EQ((int)before.bytes_written, (int)after.bytes_written);
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_close(&recent));
    TEST_ASSERT_INT_EQ(DF_OK, df_pcap_ring_close(&control));
    snprintf(path, sizeof(path), "%s/recent-000.pcap", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/control-000.pcap", directory); unlink(path);
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
}
