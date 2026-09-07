#include <stdint.h>
#include <string.h>

#include "event.h"
#include "gvs_frame.h"
#include "test.h"

static size_t make_frame(uint8_t *packet, uint8_t family, uint8_t opcode,
                         uint8_t status) {
    static const uint8_t prefix[] = {'G', 'V', 'S', 'G', 'V', 'S',
                                     0xA5, 0xA5, 0xA5, 0xA5};
    static const uint8_t destination[] = {0x32, 0, 0, 0, 0, 2};
    static const uint8_t source[] = {0x61, 0, 0, 0, 0, 1};

    memcpy(packet, prefix, sizeof(prefix));
    memcpy(packet + 10, destination, sizeof(destination));
    memcpy(packet + 16, source, sizeof(source));
    memset(packet + 22, 0, 16);
    packet[38] = family;
    packet[39] = opcode;
    packet[40] = status;
    return 41;
}

void test_gvs_frame_validation_and_event_mapping(void) {
    uint8_t packet[41] = {0};
    struct df_gvs_frame frame = {0};
    struct df_event event = {0};

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_frame_parse(packet, sizeof(packet) - 1, &frame, &event));

    (void)make_frame(packet, 0x03, 0x04, 0);
    packet[6] = 0;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_frame_parse(packet, sizeof(packet), &frame, &event));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(
                                   packet, make_frame(packet, 0x03, 0x04, 0), &frame, &event));
    TEST_ASSERT_INT_EQ(DF_EVENT_PREVIEW_STARTED, event.type);
    TEST_ASSERT_INT_EQ(0, memcmp(frame.destination,
                                 (const uint8_t[]){0x32, 0, 0, 0, 0, 2}, 6));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(
                                   packet, make_frame(packet, 0x77, 0x01, 0), &frame, &event));
    TEST_ASSERT_INT_EQ(DF_EVENT_UNKNOWN, event.type);
}

void test_gvs_event_names(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("StationObserved",
                                 df_event_type_name(DF_EVENT_STATION_OBSERVED)));
    TEST_ASSERT_INT_EQ(0, strcmp("PreviewStarted",
                                 df_event_type_name(DF_EVENT_PREVIEW_STARTED)));
    TEST_ASSERT_INT_EQ(0, strcmp("SessionEstablished",
                                 df_event_type_name(DF_EVENT_SESSION_ESTABLISHED)));
    TEST_ASSERT_INT_EQ(0, strcmp("UnlockResultObserved",
                                 df_event_type_name(DF_EVENT_UNLOCK_RESULT_OBSERVED)));
}
