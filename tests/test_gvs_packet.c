#include <stdint.h>
#include <string.h>

#include "gvs_packet.h"
#include "test.h"

static size_t build_udp_packet(uint8_t *packet, size_t capacity, bool vlan,
                               uint16_t source, uint16_t destination,
                               const uint8_t *payload, size_t declared_payload,
                               size_t captured_payload) {
    size_t ethernet = vlan ? 18 : 14;
    size_t ip = ethernet;
    size_t udp = ip + 20;
    size_t captured = udp + 8 + captured_payload;
    if (captured > capacity || declared_payload > 65507) return 0;
    memset(packet, 0, capacity);
    packet[12] = vlan ? 0x81 : 0x08;
    packet[13] = vlan ? 0x00 : 0x00;
    if (vlan) { packet[16] = 0x08; packet[17] = 0x00; }
    packet[ip] = 0x45;
    uint16_t ip_length = (uint16_t)(20 + 8 + declared_payload);
    packet[ip + 2] = (uint8_t)(ip_length >> 8);
    packet[ip + 3] = (uint8_t)ip_length;
    packet[ip + 9] = 17;
    packet[udp] = (uint8_t)(source >> 8);
    packet[udp + 1] = (uint8_t)source;
    packet[udp + 2] = (uint8_t)(destination >> 8);
    packet[udp + 3] = (uint8_t)destination;
    uint16_t udp_length = (uint16_t)(8 + declared_payload);
    packet[udp + 4] = (uint8_t)(udp_length >> 8);
    packet[udp + 5] = (uint8_t)udp_length;
    if (payload != NULL && captured_payload > 0)
        memcpy(packet + udp + 8, payload, captured_payload);
    return captured;
}

void test_gvs_udp_prefix_handles_vlan_and_truncation(void) {
    uint8_t packet[512], payload[300];
    struct df_udp_prefix prefix;
    memset(payload, 0x55, sizeof(payload));
    size_t length = build_udp_packet(packet, sizeof(packet), false, 8303, 40000,
                                     payload, 300, 214);
    TEST_ASSERT_INT_EQ(1, df_gvs_inspect_udp_prefix(packet, length, &prefix));
    TEST_ASSERT_INT_EQ(8303, prefix.source_port);
    TEST_ASSERT_INT_EQ(40000, prefix.destination_port);
    TEST_ASSERT_INT_EQ(42, (int)prefix.payload_offset);
    TEST_ASSERT_INT_EQ(214, (int)prefix.captured_payload_length);
    TEST_ASSERT_INT_EQ(300, (int)prefix.declared_payload_length);
    TEST_ASSERT_INT_EQ(0, prefix.payload_complete);

    length = build_udp_packet(packet, sizeof(packet), true, 10000, 8300,
                              payload, 42, 42);
    TEST_ASSERT_INT_EQ(1, df_gvs_inspect_udp_prefix(packet, length, &prefix));
    TEST_ASSERT_INT_EQ(46, (int)prefix.payload_offset);
    TEST_ASSERT_INT_EQ(1, prefix.payload_complete);
    TEST_ASSERT_INT_EQ(-1, df_gvs_inspect_udp_prefix(packet, 43, &prefix));

    length = build_udp_packet(packet, sizeof(packet), false, 8300, 8300,
                              payload, 42, 42);
    packet[38] = 0;
    packet[39] = 80;
    TEST_ASSERT_INT_EQ(-1, df_gvs_inspect_udp_prefix(packet, length, &prefix));
    TEST_ASSERT_INT_EQ(-1, df_gvs_inspect_udp_prefix(NULL, length, &prefix));
}
