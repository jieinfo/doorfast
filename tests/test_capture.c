#include <pcap/pcap.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "capture.h"
#include "test.h"

void test_default_capture_filter(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("udp and (port 8300 or port 8302 or port 8303 or port 8304)",
                                 df_capture_default_filter()));
}

void test_capture_next_rejects_invalid_arguments(void) {
    const uint8_t *packet = NULL;
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_CAPTURE_ERROR, df_capture_next(NULL, &packet, &length));
}

void test_capture_record_preserves_timestamp_and_wire_length(void) {
    char path[] = "/tmp/doorfast-capture-record-XXXXXX";
    int descriptor = mkstemp(path);
    pcap_t *dead;
    pcap_dumper_t *dumper;
    const uint8_t bytes[] = {0x10, 0x20, 0x30, 0x40};
    struct pcap_pkthdr header = {
        .ts = {.tv_sec = 123, .tv_usec = 456789},
        .caplen = sizeof(bytes),
        .len = 80,
    };
    struct df_capture *capture = NULL;
    struct df_capture_record record;
    const uint8_t *packet = NULL;
    size_t length = 0;

    TEST_ASSERT_INT_EQ(1, descriptor >= 0);
    if (descriptor >= 0) (void)close(descriptor);
    dead = pcap_open_dead(DLT_EN10MB, 2048);
    TEST_ASSERT_INT_EQ(1, dead != NULL);
    dumper = dead == NULL ? NULL : pcap_dump_open(dead, path);
    TEST_ASSERT_INT_EQ(1, dumper != NULL);
    if (dumper != NULL) {
        pcap_dump((u_char *)dumper, &header, bytes);
        pcap_dump_close(dumper);
    }
    if (dead != NULL) pcap_close(dead);

    TEST_ASSERT_INT_EQ(DF_OK, df_capture_open_offline(path, &capture));
    memset(&record, 0x5a, sizeof(record));
    TEST_ASSERT_INT_EQ(DF_CAPTURE_PACKET,
                       df_capture_next_record(capture, &record));
    TEST_ASSERT_INT_EQ(4, (int)record.captured_length);
    TEST_ASSERT_INT_EQ(80, (int)record.original_length);
    TEST_ASSERT_INT_EQ(123, (int)record.wall_seconds);
    TEST_ASSERT_INT_EQ(456789, (int)record.wall_microseconds);
    TEST_ASSERT_INT_EQ(0, memcmp(bytes, record.data, sizeof(bytes)));
    df_capture_close(capture);

    capture = NULL;
    TEST_ASSERT_INT_EQ(DF_OK, df_capture_open_offline(path, &capture));
    TEST_ASSERT_INT_EQ(DF_CAPTURE_PACKET,
                       df_capture_next(capture, &packet, &length));
    TEST_ASSERT_INT_EQ(4, (int)length);
    TEST_ASSERT_INT_EQ(0, memcmp(bytes, packet, sizeof(bytes)));
    df_capture_close(capture);
    (void)unlink(path);

    memset(&record, 0x5a, sizeof(record));
    TEST_ASSERT_INT_EQ(DF_CAPTURE_ERROR, df_capture_next_record(NULL, &record));
    TEST_ASSERT_INT_EQ(0, record.data != NULL);
    TEST_ASSERT_INT_EQ(0, (int)record.captured_length);
}
