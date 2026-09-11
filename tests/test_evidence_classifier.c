#include <stdint.h>
#include <string.h>

#include "evidence_classifier.h"
#include "test.h"

static size_t make_packet(uint8_t *packet, uint16_t port, size_t declared,
                          size_t captured) {
    size_t length = 42 + captured;
    memset(packet, 0, 512);
    packet[12] = 0x08; packet[13] = 0x00;
    packet[14] = 0x45;
    uint16_t ip_length = (uint16_t)(28 + declared);
    packet[16] = (uint8_t)(ip_length >> 8); packet[17] = (uint8_t)ip_length;
    packet[23] = 17;
    packet[34] = (uint8_t)(port >> 8); packet[35] = (uint8_t)port;
    packet[36] = 0x9c; packet[37] = 0x40;
    uint16_t udp_length = (uint16_t)(8 + declared);
    packet[38] = (uint8_t)(udp_length >> 8); packet[39] = (uint8_t)udp_length;
    return length;
}

void test_evidence_classifier_separates_recent_and_control(void) {
    uint8_t packet[512];
    struct df_capture_record record = {.data = packet, .original_length = 84};
    struct df_evidence_classification classification;
    size_t length = make_packet(packet, 8300, 42, 42);
    memcpy(packet + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    packet[80] = 0x03; packet[81] = 0x01;
    record.captured_length = length;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_classify(&record, &classification));
    TEST_ASSERT_INT_EQ(1, classification.recent);
    TEST_ASSERT_INT_EQ(1, classification.valid_control);
    TEST_ASSERT_INT_EQ(8300, classification.source_port);
    TEST_ASSERT_INT_EQ(0x03, classification.family);
    TEST_ASSERT_INT_EQ(0x01, classification.opcode);
    TEST_ASSERT_INT_EQ(42, (int)classification.control_length);

    length = make_packet(packet, 8303, 300, 214);
    record.captured_length = length;
    record.original_length = 342;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_classify(&record, &classification));
    TEST_ASSERT_INT_EQ(1, classification.recent);
    TEST_ASSERT_INT_EQ(0, classification.valid_control);

    length = make_packet(packet, 9000, 8, 8);
    record.captured_length = record.original_length = length;
    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_classify(&record, &classification));
    TEST_ASSERT_INT_EQ(0, classification.recent);
    TEST_ASSERT_INT_EQ(0, classification.valid_control);

    length = make_packet(packet, 8300, 42, 42);
    packet[38] = 0; packet[39] = 80;
    record.captured_length = record.original_length = length;
    memset(&classification, 0x5a, sizeof(classification));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_evidence_classify(&record, &classification));
    TEST_ASSERT_INT_EQ(0, classification.recent);
    TEST_ASSERT_INT_EQ(0, classification.valid_control);
}

void test_evidence_classifier_keeps_observed_incoming_call(void) {
    uint8_t packet[512];
    struct df_capture_record record = {.data = packet};
    struct df_evidence_classification classification;
    const uint8_t payload[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    size_t length = make_packet(packet, 8300, 51, 51);

    memcpy(packet + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    memset(packet + 52, 0, 28);
    packet[80] = 0x03;
    packet[81] = 0x01;
    packet[82] = 15;
    packet[83] = 0;
    memcpy(packet + 84, payload, sizeof(payload));
    record.captured_length = record.original_length = length;

    TEST_ASSERT_INT_EQ(DF_OK, df_evidence_classify(&record, &classification));
    TEST_ASSERT_INT_EQ(1, classification.recent);
    TEST_ASSERT_INT_EQ(1, classification.valid_control);
    TEST_ASSERT_INT_EQ(0x03, classification.family);
    TEST_ASSERT_INT_EQ(0x01, classification.opcode);
    TEST_ASSERT_INT_EQ(51, (int)classification.control_length);
}
