#include <arpa/inet.h>
#include <string.h>

#include "gvs_station_discovery.h"
#include "test.h"

enum {
    DISCOVERY_ETHERNET_SIZE = 14,
    DISCOVERY_IPV4_SIZE = 20,
    DISCOVERY_UDP_SIZE = 8,
    DISCOVERY_CONTROL_SIZE = 42,
    DISCOVERY_PACKET_SIZE = DISCOVERY_ETHERNET_SIZE + DISCOVERY_IPV4_SIZE +
        DISCOVERY_UDP_SIZE + DISCOVERY_CONTROL_SIZE,
    DISCOVERY_STATUS_PAYLOAD_SIZE = 2,
    DISCOVERY_STATUS_PACKET_SIZE = DISCOVERY_PACKET_SIZE +
        DISCOVERY_STATUS_PAYLOAD_SIZE,
};

static uint8_t discovery_bcd(unsigned value) {
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static size_t discovery_reply_packet(uint8_t *packet, size_t capacity,
    const uint8_t destination[6], const uint8_t source[6],
    const uint8_t source_ipv4[4], uint16_t destination_port,
    uint8_t opcode) {
    const uint16_t ip_length = DISCOVERY_IPV4_SIZE + DISCOVERY_UDP_SIZE +
        DISCOVERY_CONTROL_SIZE;
    const uint16_t udp_length = DISCOVERY_UDP_SIZE + DISCOVERY_CONTROL_SIZE;
    const size_t udp = DISCOVERY_ETHERNET_SIZE + DISCOVERY_IPV4_SIZE;
    const size_t control = udp + DISCOVERY_UDP_SIZE;

    if (packet == NULL || destination == NULL || source == NULL ||
        source_ipv4 == NULL || capacity < DISCOVERY_PACKET_SIZE)
        return 0U;
    memset(packet, 0, capacity);
    packet[12] = 0x08U;
    packet[13] = 0x00U;
    packet[14] = 0x45U;
    packet[16] = (uint8_t)(ip_length >> 8U);
    packet[17] = (uint8_t)ip_length;
    packet[23] = 17U;
    memcpy(packet + 26U, source_ipv4, 4U);
    memcpy(packet + 30U, (const uint8_t[]){10U, 0U, 0U, 2U}, 4U);
    packet[udp] = (uint8_t)(8300U >> 8U);
    packet[udp + 1U] = (uint8_t)8300U;
    packet[udp + 2U] = (uint8_t)(destination_port >> 8U);
    packet[udp + 3U] = (uint8_t)destination_port;
    packet[udp + 4U] = (uint8_t)(udp_length >> 8U);
    packet[udp + 5U] = (uint8_t)udp_length;
    memcpy(packet + control, "GVSGVS\xA5\xA5\xA5\xA5", 10U);
    memcpy(packet + control + 10U, destination, 6U);
    memcpy(packet + control + 16U, source, 6U);
    packet[control + 38U] = 0x07U;
    packet[control + 39U] = opcode;
    packet[control + 40U] = 0U;
    packet[control + 41U] = 0U;
    return DISCOVERY_PACKET_SIZE;
}

static size_t discovery_reply_status_packet(uint8_t *packet, size_t capacity,
    const uint8_t destination[6], const uint8_t source[6],
    const uint8_t source_ipv4[4], uint16_t destination_port) {
    const size_t udp = DISCOVERY_ETHERNET_SIZE + DISCOVERY_IPV4_SIZE;
    const size_t control = udp + DISCOVERY_UDP_SIZE;
    const uint16_t ip_length = DISCOVERY_IPV4_SIZE + DISCOVERY_UDP_SIZE +
        DISCOVERY_CONTROL_SIZE + DISCOVERY_STATUS_PAYLOAD_SIZE;
    const uint16_t udp_length = DISCOVERY_UDP_SIZE + DISCOVERY_CONTROL_SIZE +
        DISCOVERY_STATUS_PAYLOAD_SIZE;
    size_t length = discovery_reply_packet(packet, capacity, destination,
        source, source_ipv4, destination_port, 0x86U);

    if (length == 0U || capacity < DISCOVERY_STATUS_PACKET_SIZE) return 0U;
    packet[16] = (uint8_t)(ip_length >> 8U);
    packet[17] = (uint8_t)ip_length;
    packet[udp + 4U] = (uint8_t)(udp_length >> 8U);
    packet[udp + 5U] = (uint8_t)udp_length;
    packet[control + 40U] = 2U;
    packet[control + 42U] = 0x02U;
    packet[control + 43U] = 0x00U;
    return DISCOVERY_STATUS_PACKET_SIZE;
}

static const struct df_gvs_station_candidate *discovery_find_candidate(
    const struct df_gvs_station_discovery *discovery,
    const uint8_t logical_address[6]) {
    size_t index;

    for (index = 0U; index < DF_GVS_STATION_CANDIDATE_CAPACITY; index++) {
        if (discovery->candidates[index].valid &&
            memcmp(discovery->candidates[index].logical_address,
                logical_address, 6U) == 0)
            return &discovery->candidates[index];
    }
    return NULL;
}

void test_gvs_station_scan_emits_exact_three_frame_intents(void) {
    const uint8_t identity[6] = {0x61U, 0x02U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t destination[6] = {
        0x32U, 0x02U, 0x01U, 0xffU, 0xffU, 0xffU};
    const uint8_t payload[4] = {0x02U, 0x00U, 0x00U, 0x01U};
    struct df_gvs_station_scan scan = {0};
    struct df_gvs_station_scan_action action = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_scan_start(
        &scan, identity, 1000U));
    TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 1000U, &action));
    TEST_ASSERT_INT_EQ(0, memcmp(destination, action.destination, 6U));
    TEST_ASSERT_INT_EQ(0, memcmp(identity, action.source, 6U));
    TEST_ASSERT_INT_EQ(0x07, action.family);
    TEST_ASSERT_INT_EQ(0x06, action.opcode);
    TEST_ASSERT_INT_EQ(4, (int)action.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(payload, action.payload, sizeof(payload)));
    TEST_ASSERT_INT_EQ(0, df_gvs_station_scan_next(&scan, 1499U, &action));
    TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 1500U, &action));
    TEST_ASSERT_INT_EQ(0, df_gvs_station_scan_next(&scan, 1999U, &action));
    TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 2000U, &action));
    TEST_ASSERT_INT_EQ(0, df_gvs_station_scan_next(&scan, 2500U, &action));
}

void test_gvs_station_discovery_strictly_admits_and_refreshes_replies(void) {
    const uint8_t identity[6] = {0x61U, 0x02U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t station[6] = {0x32U, 0x02U, 0x01U, 0U, 0x01U, 0U};
    const uint8_t wrong_building[6] = {0x32U, 0x03U, 0x01U, 0U, 0x01U, 0U};
    const uint8_t wrong_unit[6] = {0x32U, 0x02U, 0x02U, 0U, 0x01U, 0U};
    const uint8_t source_ipv4[4] = {10U, 2U, 3U, 4U};
    const uint8_t refreshed_ipv4[4] = {10U, 2U, 3U, 5U};
    const uint8_t multicast_ipv4[4] = {239U, 1U, 2U, 3U};
    const uint8_t invalid_identity[6] = {
        0x60U, 0x02U, 0x01U, 0x01U, 0x01U, 0x01U};
    struct df_gvs_station_discovery discovery = {0};
    const struct df_gvs_station_candidate *candidate;
    uint8_t packet[DISCOVERY_PACKET_SIZE];
    uint8_t wrong_destination[6];
    size_t length;

    length = discovery_reply_packet(packet, sizeof(packet), invalid_identity,
        station, source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, invalid_identity, 0U));
    length = discovery_reply_packet(packet, sizeof(packet), identity,
        wrong_building, source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 1U));
    length = discovery_reply_packet(packet, sizeof(packet), identity,
        wrong_unit, source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 2U));

    memcpy(wrong_destination, identity, sizeof(wrong_destination));
    wrong_destination[5] = 0x02U;
    length = discovery_reply_packet(packet, sizeof(packet), wrong_destination,
        station, source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 3U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x86U);
    packet[DISCOVERY_ETHERNET_SIZE + DISCOVERY_IPV4_SIZE +
        DISCOVERY_UDP_SIZE + 38U] = 0x08U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 4U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x85U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 5U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        multicast_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 6U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 0U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 7U));

    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x86U);
    packet[DISCOVERY_PACKET_SIZE - 2U] = 1U;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 8U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_discovery_observe(
        &discovery, packet, length - 1U, identity, 9U));
    TEST_ASSERT_INT_EQ(0, (int)discovery.count);

    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 100U));
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        refreshed_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 110U));
    TEST_ASSERT_INT_EQ(1, (int)discovery.count);
    candidate = discovery_find_candidate(&discovery, station);
    TEST_ASSERT_INT_EQ(1, candidate != NULL);
    if (candidate != NULL) {
        TEST_ASSERT_INT_EQ(0, memcmp(&candidate->ipv4, refreshed_ipv4, 4U));
        TEST_ASSERT_INT_EQ(100, (int)candidate->first_seen_ms);
        TEST_ASSERT_INT_EQ(110, (int)candidate->last_seen_ms);
        TEST_ASSERT_INT_EQ(2, (int)candidate->reply_count);
    }
}

void test_gvs_station_discovery_accepts_captured_status_payload(void) {
    const uint8_t identity[6] = {0x61U, 0x02U, 0x01U, 0x19U, 0x01U, 0x01U};
    const uint8_t station[6] = {0x32U, 0x02U, 0x01U, 0U, 0x03U, 0U};
    const uint8_t source_ipv4[4] = {10U, 5U, 64U, 16U};
    struct df_gvs_station_discovery discovery = {0};
    uint8_t packet[DISCOVERY_STATUS_PACKET_SIZE];
    size_t length = discovery_reply_status_packet(packet, sizeof(packet),
        identity, station, source_ipv4, 56938U);

    TEST_ASSERT_INT_EQ(DISCOVERY_STATUS_PACKET_SIZE, (int)length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 100U));
    TEST_ASSERT_INT_EQ(1, (int)discovery.count);
}

void test_gvs_station_discovery_evicts_least_recently_seen_candidate(void) {
    const uint8_t identity[6] = {0x61U, 0x02U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t source_ipv4[4] = {10U, 2U, 3U, 4U};
    struct df_gvs_station_discovery discovery = {0};
    uint8_t packet[DISCOVERY_PACKET_SIZE];
    uint8_t station[6] = {0x32U, 0x02U, 0x01U, 0U, 0U, 0U};
    uint8_t first[6];
    uint8_t second[6];
    size_t length;
    unsigned index;

    for (index = 1U; index <= DF_GVS_STATION_CANDIDATE_CAPACITY; index++) {
        station[4] = discovery_bcd(index);
        length = discovery_reply_packet(packet, sizeof(packet), identity,
            station, source_ipv4, 8300U, 0x86U);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
            &discovery, packet, length, identity, index));
    }
    TEST_ASSERT_INT_EQ(64, (int)discovery.count);
    memcpy(first, (const uint8_t[]){0x32U, 0x02U, 0x01U, 0U, 0x01U, 0U}, 6U);
    memcpy(second, (const uint8_t[]){0x32U, 0x02U, 0x01U, 0U, 0x02U, 0U}, 6U);
    length = discovery_reply_packet(packet, sizeof(packet), identity, first,
        source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 1000U));
    station[4] = discovery_bcd(65U);
    length = discovery_reply_packet(packet, sizeof(packet), identity, station,
        source_ipv4, 8300U, 0x86U);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_discovery_observe(
        &discovery, packet, length, identity, 1001U));

    TEST_ASSERT_INT_EQ(64, (int)discovery.count);
    TEST_ASSERT_INT_EQ(1, discovery_find_candidate(&discovery, first) != NULL);
    TEST_ASSERT_INT_EQ(0, discovery_find_candidate(&discovery, second) != NULL);
    TEST_ASSERT_INT_EQ(1, discovery_find_candidate(&discovery, station) != NULL);
}
