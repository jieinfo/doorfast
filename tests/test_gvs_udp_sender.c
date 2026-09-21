#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#include "gvs_call_command.h"
#include "gvs_audio_tx.h"
#include "gvs_elevator.h"
#include "gvs_media.h"
#include "gvs_multicast.h"
#include "gvs_pcm_ingress.h"
#include "gvs_pcm_pump.h"
#include "gvs_serialize.h"
#include "gvs_sync.h"
#include "gvs_runtime_sync.h"
#include "gvs_udp_sender.h"
#include "runtime_service.h"
#include "test.h"

static size_t udp_sender_packet(uint8_t *, size_t, const uint8_t [6],
    const uint8_t [6], const uint8_t [4]);

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

void test_gvs_udp_sender_binds_configured_source_address(void) {
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_multicast multicast = {.fd = -1};
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int reuse = 0;
    socklen_t reuse_length = sizeof(reuse);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open_bound(&sender,
        "127.0.0.1", 8300U, 8300U, udp_sender_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, getsockname(sender.fd,
        (struct sockaddr *)&address, &length));
    TEST_ASSERT_INT_EQ(AF_INET, address.sin_family);
    TEST_ASSERT_INT_EQ(htonl(INADDR_LOOPBACK), address.sin_addr.s_addr);
    TEST_ASSERT_INT_EQ(8300, ntohs(address.sin_port));
    TEST_ASSERT_INT_EQ(0, getsockopt(sender.fd, SOL_SOCKET, SO_REUSEADDR,
        &reuse, &reuse_length));
    TEST_ASSERT_INT_EQ(1, reuse != 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_multicast_open_group(&multicast,
        "239.1.2.3", "127.0.0.1"));
    df_gvs_multicast_close(&multicast);
    df_gvs_udp_sender_close(&sender);
}

void test_gvs_udp_sender_broadcasts_exact_station_scan_frame(void) {
    const uint8_t identity[6] = {0x61U, 0x02U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t logical_broadcast[6] = {
        0x32U, 0x02U, 0x01U, 0xffU, 0xffU, 0xffU};
    const uint8_t payload[4] = {0x02U, 0x00U, 0x00U, 0x01U};
    struct df_gvs_station_scan scan = {0};
    struct df_gvs_station_scan_action action = {0};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct sockaddr_in address;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    uint8_t received[128];
    int broadcast = 0;
    socklen_t broadcast_length = sizeof(broadcast);
    int receiver;
    ssize_t received_length;

    receiver = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_INT_EQ(1, receiver >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(8300U);
    TEST_ASSERT_INT_EQ(0, bind(receiver, (const struct sockaddr *)&address,
        sizeof(address)));
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
        &timeout, sizeof(timeout)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(&sender, "0.0.0.0",
        8301U, udp_sender_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, getsockopt(sender.fd, SOL_SOCKET, SO_BROADCAST,
        &broadcast, &broadcast_length));
    TEST_ASSERT_INT_EQ(1, broadcast != 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_scan_start(&scan, identity, 1U));
    TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 1U, &action));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_gvs_udp_sender_emit_station_scan(&sender, &action));
    received_length = recv(receiver, received, sizeof(received), 0);

    TEST_ASSERT_INT_EQ(46, (int)received_length);
    if (received_length == 46) {
        TEST_ASSERT_INT_EQ(0,
            memcmp(received, "GVSGVS\xA5\xA5\xA5\xA5", 10U));
        TEST_ASSERT_INT_EQ(0,
            memcmp(received + 10U, logical_broadcast, 6U));
        TEST_ASSERT_INT_EQ(0, memcmp(received + 16U, identity, 6U));
        TEST_ASSERT_INT_EQ(0,
            memcmp(received + 22U, (const uint8_t[]){
                0x31U, 0x31U, 0x31U, 0x31U,
                0x31U, 0x31U, 0x31U, 0x31U}, 8U));
        TEST_ASSERT_INT_EQ(0,
            memcmp(received + 30U, (const uint8_t[]){
                0x41U, 0x41U, 0x41U, 0x41U,
                0x41U, 0x41U, 0x41U, 0x41U}, 8U));
        TEST_ASSERT_INT_EQ(0x07, received[38]);
        TEST_ASSERT_INT_EQ(0x06, received[39]);
        TEST_ASSERT_INT_EQ(4, received[40]);
        TEST_ASSERT_INT_EQ(0, received[41]);
        TEST_ASSERT_INT_EQ(0, memcmp(received + 42U, payload, sizeof(payload)));
    }
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);
    TEST_ASSERT_INT_EQ(0, (int)sender.failed);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}

void test_gvs_udp_presence_treats_unsendable_presence_actions_as_local_only(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_udp_presence_context context = {
        .sender = &sender,
        .source = local,
        .sync_version = 0,
    };
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PEER_OFFLINE,
        .target = {0x61, 2, 1, 1, 1, 2},
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(&sender, "127.0.0.1",
        8300, udp_sender_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_presence_emit(&action, &context));
    TEST_ASSERT_INT_EQ(0, (int)context.emitted_packets);
    action.type = DF_GVS_PRESENCE_PERIODIC_SYNC;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_presence_emit(&action, &context));
    TEST_ASSERT_INT_EQ(0, (int)context.emitted_packets);
    TEST_ASSERT_INT_EQ(0, (int)sender.failed);
    df_gvs_udp_sender_close(&sender);
}

void test_gvs_udp_sender_transmits_peer_reply_with_runtime_header(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_udp_presence_context context = {
        .sender = &sender,
        .source = local,
    };
    const struct df_gvs_reply_queue_entry entry = {
        .reply = {
            .target = {0x61, 2, 1, 1, 1, 2},
            .request_data = {0x12, 0x34},
        },
    };
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
    packet_length = udp_sender_packet(packet, sizeof(packet), local, peer,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));

    TEST_ASSERT_INT_EQ(DF_GVS_SEND_ATTEMPT_SUCCESS,
        df_gvs_udp_peer_reply_send_attempt(&entry, 1U, 7U, &context));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(48, (int)received_length);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 10U, peer, sizeof(peer)));
    TEST_ASSERT_INT_EQ(0, memcmp(received + 16U, local, sizeof(local)));
    TEST_ASSERT_INT_EQ(0x07, received[38]);
    TEST_ASSERT_INT_EQ(0x81, received[39]);
    TEST_ASSERT_INT_EQ(0x12, received[42]);
    TEST_ASSERT_INT_EQ(0x34, received[43]);
    TEST_ASSERT_INT_EQ(0x31, received[22]);
    TEST_ASSERT_INT_EQ(0x41, received[30]);
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}

void test_gvs_udp_sender_transmits_periodic_sync_and_counts_packets(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_runtime_sync sync;
    struct df_runtime_config runtime = {0};
    struct df_gvs_udp_presence_context context = {
        .sender = &sender,
        .source = local,
        .sync_version = 9U,
        .store = &sync.store,
    };
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PERIODIC_SYNC,
        .target = {0x61, 2, 1, 1, 1, 2},
    };
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    uint8_t packet[128];
    uint8_t received[DF_GVS_SYNC_MAX_PACKET_SIZE];
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
    packet_length = udp_sender_packet(packet, sizeof(packet), local, peer,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));
    runtime.config.active_host = true;
    runtime.config.sync_mini1_secretkey = "mini-one";
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_start(
        &sync, local, 9U, 0U));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_sync_configure(&sync, &runtime));
    TEST_ASSERT_INT_EQ(1, (int)sync.store.count);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_presence_emit(&action, &context));
    TEST_ASSERT_INT_EQ(1, (int)context.emitted_packets);
    memset(received, 0, sizeof(received));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(1, received_length > 42 ? 1 : 0);
    TEST_ASSERT_INT_EQ(0x91, received[38]);
    TEST_ASSERT_INT_EQ(0x03, received[39]);
    TEST_ASSERT_INT_EQ(9, received[42]);
    TEST_ASSERT_INT_EQ(0, received[43]);
    TEST_ASSERT_INT_EQ(1, strstr((const char *)received + 44,
        "\"KEY\":\"sync_mini1_secretkey\",\"VALUE\":\"mini-one\"") != NULL);
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);

    df_gvs_sync_store_init(&sync.store);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_presence_emit(&action, &context));
    TEST_ASSERT_INT_EQ(0, (int)context.emitted_packets);
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
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

static size_t udp_sender_preview_packet(uint8_t *packet, size_t capacity,
    const uint8_t destination[6], const uint8_t source[6],
    const uint8_t source_ipv4[4]) {
    uint8_t control[64];
    size_t control_length = 0U;
    size_t udp_offset = 14U + 20U;
    size_t packet_length;
    uint16_t ip_length;
    uint16_t udp_length;

    if (df_gvs_control_serialize(control, sizeof(control), &control_length,
            destination, source, 0x07U, 0x86U, NULL, 0U,
            udp_sender_header_fields, NULL) != DF_OK)
        return 0U;
    packet_length = udp_offset + 8U + control_length;
    if (packet_length > capacity) return 0U;
    memset(packet, 0, capacity);
    packet[12] = 0x08U;
    packet[13] = 0x00U;
    packet[14] = 0x45U;
    ip_length = (uint16_t)(20U + 8U + control_length);
    packet[16] = (uint8_t)(ip_length >> 8U);
    packet[17] = (uint8_t)ip_length;
    packet[23] = 17U;
    memcpy(packet + 26U, source_ipv4, 4U);
    packet[30] = 10U;
    packet[31] = 0U;
    packet[32] = 0U;
    packet[33] = 2U;
    packet[udp_offset] = (uint8_t)(8300U >> 8U);
    packet[udp_offset + 1U] = (uint8_t)8300U;
    packet[udp_offset + 2U] = (uint8_t)(8300U >> 8U);
    packet[udp_offset + 3U] = (uint8_t)8300U;
    udp_length = (uint16_t)(8U + control_length);
    packet[udp_offset + 4U] = (uint8_t)(udp_length >> 8U);
    packet[udp_offset + 5U] = (uint8_t)udp_length;
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
        &sender, packet, packet_length, local, 100U));
    packet_length = udp_sender_packet(packet, sizeof(packet), local, door,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));
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

void test_gvs_udp_sender_expires_and_refreshes_observed_peer_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t door[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    uint8_t packet[128];
    size_t packet_length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(&sender, "0.0.0.0",
        8300U, udp_sender_header_fields, NULL));
    packet_length = udp_sender_packet(packet, sizeof(packet), local, door,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));
    TEST_ASSERT_INT_EQ(1, sender.observed_routes[0].valid);
    TEST_ASSERT_INT_EQ(100, (int)sender.observed_routes[0].observed_ms);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_advance(
        &sender, 100U + DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS - 1U));
    TEST_ASSERT_INT_EQ(1, sender.observed_routes[0].valid);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_advance(
        &sender, 100U + DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS));
    TEST_ASSERT_INT_EQ(0, sender.observed_routes[0].valid);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local,
        101U + DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS));
    TEST_ASSERT_INT_EQ(1, sender.observed_routes[1].valid);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_udp_sender_advance(
        &sender, DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS));
    df_gvs_udp_sender_close(&sender);
}

void test_gvs_udp_sender_emits_exact_control_to_configured_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    const uint8_t payload[] = {0x02, 0x20, 0x6f, 0x00, 0x20, 0x6e, 0x1e};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    uint8_t received[128];
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
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_udp_sender_emit_control(
        &sender, station, 0U, local, 0x03, 0x04, payload, sizeof(payload)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_set_configured_route(
        &sender, station, address.sin_addr.s_addr));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_emit_control(
        &sender, station, address.sin_addr.s_addr, local, 0x03, 0x04,
        payload, sizeof(payload)));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(49, (int)received_length);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 10, station, sizeof(station)));
    TEST_ASSERT_INT_EQ(0, memcmp(received + 16, local, sizeof(local)));
    TEST_ASSERT_INT_EQ(0x03, received[38]);
    TEST_ASSERT_INT_EQ(0x04, received[39]);
    TEST_ASSERT_INT_EQ((int)sizeof(payload), received[40]);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 42, payload, sizeof(payload)));
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}

void test_gvs_udp_sender_accepts_only_fresh_observed_preview_routes(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    uint8_t packet[128];
    uint32_t ipv4 = 0U;
    size_t packet_length;

    packet_length = udp_sender_preview_packet(packet, sizeof(packet), local,
        station, loopback);
    TEST_ASSERT_INT_EQ(1, packet_length > 0U);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_preview_route(&sender,
        packet, packet_length, local, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_resolve_preview_route(&sender,
        station, 1099U, 1000U, &ipv4));
    TEST_ASSERT_INT_EQ((int)htonl(INADDR_LOOPBACK), (int)ipv4);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_gvs_udp_sender_resolve_preview_route(&sender, station, 1100U,
            1000U, &ipv4));
}

void test_gvs_udp_sender_emits_elevator_request_to_observed_route(void) {
    const uint8_t local[6] = {0x61, 2, 1, 0x16, 1, 1};
    const uint8_t elevator[6] = {0x35, 2, 1, 0, 1, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_elevator_request request;
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
    packet_length = udp_sender_packet(packet, sizeof(packet), local, elevator,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
        local, DF_GVS_ELEVATOR_DOWN, &request));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_elevator_emit(&request, &sender));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(46, (int)received_length);
    TEST_ASSERT_INT_EQ(0x08, received[38]);
    TEST_ASSERT_INT_EQ(0x02, received[39]);
    TEST_ASSERT_INT_EQ(0, memcmp(received + 10, elevator, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(received + 16, local, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(received + 42,
        (const uint8_t[]){0x00, 0x10, 0x16, 0x01}, 4));
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}

void test_gvs_udp_sender_emits_audio_to_observed_peer_port(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t door[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_session session = {
        .state = DF_GVS_TALKING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };
    struct df_gvs_audio_tx tx;
    struct df_gvs_audio_packet audio;
    struct sockaddr_in address;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    uint8_t packet[256];
    uint8_t received[256];
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES] = {0};
    size_t packet_length;
    int receiver;
    ssize_t received_length;

    receiver = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_INT_EQ(1, receiver >= 0);
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_REUSEADDR,
                                     &(int){1}, sizeof(int)));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8302U);
    TEST_ASSERT_INT_EQ(0, bind(receiver, (const struct sockaddr *)&address,
                               sizeof(address)));
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                                     &timeout, sizeof(timeout)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(&sender, "0.0.0.0",
        8300, udp_sender_header_fields, NULL));
    packet_length = udp_sender_packet(packet, sizeof(packet), local, door,
                                      loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, packet, packet_length, local, 100U));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &tx, 400, df_gvs_udp_audio_emit, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_start(
        &tx, &session, 7, local, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_submit_pcm(
        &tx, 7, pcm, DF_GVS_AUDIO_TX_SAMPLES, 1000));
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(202, (int)received_length);
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(
        received, (size_t)received_length, &audio));
    TEST_ASSERT_INT_EQ(400, audio.sequence);
    TEST_ASSERT_INT_EQ(160, (int)audio.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(audio.destination, door, sizeof(door)));
    TEST_ASSERT_INT_EQ(0, memcmp(audio.source, local, sizeof(local)));
    TEST_ASSERT_INT_EQ(1, (int)sender.sent);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}

void test_gvs_local_pcm_reaches_observed_peer_audio_route(void)
{
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t door[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t loopback[4] = {127, 0, 0, 1};
    struct df_gvs_session session = {
        .state = DF_GVS_TALKING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 19,
    };
    struct df_gvs_udp_sender sender = {.fd = -1};
    struct df_gvs_pcm_ingress ingress = {.fd = -1};
    struct df_gvs_pcm_pump_result pump_result;
    struct df_gvs_audio_tx tx;
    struct df_gvs_audio_packet audio;
    struct sockaddr_in udp_address;
    struct sockaddr_un local_address;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES] = {0};
    uint8_t control_packet[256];
    uint8_t local_packet[DF_GVS_PCM_INGRESS_PACKET_SIZE];
    uint8_t received[256];
    char path[96];
    size_t control_length;
    size_t local_length = 0;
    ssize_t received_length;
    int receiver;
    int producer;
    size_t index;

    receiver = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_INT_EQ(1, receiver >= 0);
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_REUSEADDR,
                                     &(int){1}, sizeof(int)));
    memset(&udp_address, 0, sizeof(udp_address));
    udp_address.sin_family = AF_INET;
    udp_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    udp_address.sin_port = htons(8302U);
    TEST_ASSERT_INT_EQ(0, bind(receiver,
        (const struct sockaddr *)&udp_address, sizeof(udp_address)));
    TEST_ASSERT_INT_EQ(0, setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                                     &timeout, sizeof(timeout)));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_open(
        &sender, "0.0.0.0", 8300, udp_sender_header_fields, NULL));
    control_length = udp_sender_packet(control_packet, sizeof(control_packet),
                                       local, door, loopback);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_udp_sender_observe_peer(
        &sender, control_packet, control_length, local, 5000U));

    (void)snprintf(path, sizeof(path), "/tmp/doorfast-e2e-%ld.sock",
                   (long)getpid());
    unlink(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_ingress_open(&ingress, path));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_init(
        &tx, 900, df_gvs_udp_audio_emit, &sender));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_audio_tx_start(
        &tx, &session, session.generation, local, 5000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_ingress_serialize(
        session.generation, pcm, DF_GVS_AUDIO_TX_SAMPLES, local_packet,
        sizeof(local_packet), &local_length));

    producer = socket(AF_UNIX, SOCK_DGRAM, 0);
    TEST_ASSERT_INT_EQ(1, producer >= 0);
    memset(&local_address, 0, sizeof(local_address));
    local_address.sun_family = AF_UNIX;
    memcpy(local_address.sun_path, path, strlen(path) + 1U);
    TEST_ASSERT_INT_EQ((int)local_length, (int)sendto(producer, local_packet,
        local_length, 0, (const struct sockaddr *)&local_address,
        sizeof(local_address)));
    close(producer);

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_pcm_pump(
        &ingress, &tx, 5000, &pump_result));
    TEST_ASSERT_INT_EQ(1, (int)pump_result.received);
    TEST_ASSERT_INT_EQ(1, (int)pump_result.transmitted);
    received_length = recv(receiver, received, sizeof(received), 0);
    TEST_ASSERT_INT_EQ(202, (int)received_length);
    TEST_ASSERT_INT_EQ(0, df_gvs_parse_audio(
        received, (size_t)received_length, &audio));
    TEST_ASSERT_INT_EQ(900, audio.sequence);
    TEST_ASSERT_INT_EQ(160, (int)audio.field_c);
    TEST_ASSERT_INT_EQ(1, audio.field_d);
    TEST_ASSERT_INT_EQ(1, audio.field_e);
    TEST_ASSERT_INT_EQ(0x100, audio.field_f);
    TEST_ASSERT_INT_EQ(160, (int)audio.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(audio.destination, door, sizeof(door)));
    TEST_ASSERT_INT_EQ(0, memcmp(audio.source, local, sizeof(local)));
    for (index = 0; index < audio.payload_length; index++) {
        TEST_ASSERT_INT_EQ(0xd5, audio.payload[index]);
    }

    df_gvs_pcm_ingress_close(&ingress);
    df_gvs_udp_sender_close(&sender);
    close(receiver);
}
