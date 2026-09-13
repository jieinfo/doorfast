#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "gvs_call_command.h"
#include "gvs_serialize.h"
#include "gvs_udp_sender.h"
#include "test.h"

static int udp_sender_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)context;
    memset(random_code, 0x31, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x41, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

static size_t udp_sender_packet(uint8_t *packet, size_t capacity,
    const uint8_t destination[6], const uint8_t source[6],
    const uint8_t source_ipv4[4]) {
    uint8_t control[64];
    const uint8_t payload[] = {0x01};
    size_t control_length = 0;
    size_t udp_offset = 14U + 20U;
    size_t packet_length;
    uint16_t ip_length;
    uint16_t udp_length;

    if (df_gvs_control_serialize(control, sizeof(control), &control_length,
            destination, source, 0x03, 0x02, payload, sizeof(payload),
            udp_sender_header_fields, NULL) != DF_OK)
        return 0;
    packet_length = udp_offset + 8U + control_length;
    if (packet_length > capacity) return 0;
    memset(packet, 0, capacity);
    packet[12] = 0x08;
    packet[13] = 0x00;
    packet[14] = 0x45;
    ip_length = (uint16_t)(20U + 8U + control_length);
    packet[16] = (uint8_t)(ip_length >> 8);
    packet[17] = (uint8_t)ip_length;
    packet[23] = 17;
    memcpy(packet + 26, source_ipv4, 4);
    packet[30] = 10;
    packet[31] = 0;
    packet[32] = 0;
    packet[33] = 2;
    packet[udp_offset] = (uint8_t)(8300U >> 8);
    packet[udp_offset + 1] = (uint8_t)8300U;
    packet[udp_offset + 2] = (uint8_t)(8300U >> 8);
    packet[udp_offset + 3] = (uint8_t)8300U;
    udp_length = (uint16_t)(8U + control_length);
    packet[udp_offset + 4] = (uint8_t)(udp_length >> 8);
    packet[udp_offset + 5] = (uint8_t)udp_length;
    memcpy(packet + udp_offset + 8U, control, control_length);
    return packet_length;
}

void test_gvs_udp_sender_replies_to_observed_peer_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t other_local[6] = {0x61, 9, 9, 9, 9, 1};
    const uint8_t door[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };
    struct df_gvs_call_command command;
    struct df_gvs_incoming_reply incoming_reply;
    struct df_gvs_receive_result observed = {.accepted_call = true};
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    uint8_t packet[128];
    uint8_t received[128];
    size_t packet_length;
    int receiver;
    ssize_t received_length;

    receiver = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_INT_EQ(1, receiver >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_INT_EQ(0, bind(receiver, (const struct sockaddr *)&address,
                               sizeof(address)));
    TEST_ASSERT_INT_EQ(0, getsockname(receiver, (struct sockaddr *)&address,
                                      &address_length));
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                                     &timeout, sizeof(timeout)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(&sender, "0.0.0.0",
        ntohs(address.sin_port), udp_sender_header_fields, NULL));

    packet_length = udp_sender_packet(packet, sizeof(packet), other_local,
                                      door, loopback);
    TEST_ASSERT_INT_EQ(1, packet_length > 0);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local));
    packet_length = udp_sender_packet(packet, sizeof(packet), local, door,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_incoming_reply_prepare(
        &observed, &session, 7, local, 8303, &incoming_reply));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_incoming_reply_emit(
        &incoming_reply, &sender));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(49, (int)received_length);
    TEST_ASSERT_INT_EQ(0x81, received[39]);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 10, door, sizeof(door)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_command_prepare_answer(
        &session, 7, local, 8303, 8302, 120, &command));
    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_SUCCESS, df_gvs_udp_send_attempt(
        &command, 1, 1, &sender));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(49, (int)received_length);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 10, door, sizeof(door)));

    {
        struct df_gvs_access_request access;
        const uint8_t material[8] = {1,2,3,4,5,6,7,8};
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_prepare_direct(
            &session, 7, local, material, &access));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_access_emit(&access, &sender));
        received_length = recv(receiver, received, sizeof(received), 0);
        TEST_ASSERT_INT_EQ(50, (int)received_length);
        TEST_ASSERT_INT_EQ(4, received[38]);
        TEST_ASSERT_INT_EQ(9, received[39]);
        TEST_ASSERT_INT_EQ(12, received[40]);
        TEST_ASSERT_INT_EQ(0, memcmp(received + 42, material, 8));
    }
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}
