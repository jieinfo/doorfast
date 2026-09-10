#include "gvs_packet.h"

static uint16_t df_read_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

int df_gvs_inspect_udp_prefix(const uint8_t *packet, size_t length,
                              struct df_udp_prefix *output) {
    struct df_udp_prefix prefix = {0};
    size_t ethernet_length = 14;
    size_t ip_length, ip_total_length, udp_offset, udp_length;
    uint16_t ether_type;

    if (output != NULL) *output = prefix;
    if (packet == NULL || output == NULL || length < ethernet_length) return -1;
    ether_type = df_read_be16(packet + 12);
    if (ether_type == 0x8100 || ether_type == 0x88a8) {
        if (length < ethernet_length + 4) return -1;
        ether_type = df_read_be16(packet + ethernet_length + 2);
        ethernet_length += 4;
        if (ether_type == 0x8100 || ether_type == 0x88a8) return -1;
    }
    if (ether_type != 0x0800) return 0;
    if (length < ethernet_length + 20 || (packet[ethernet_length] >> 4) != 4)
        return -1;
    ip_length = (size_t)(packet[ethernet_length] & 0x0f) * 4;
    ip_total_length = df_read_be16(packet + ethernet_length + 2);
    if (ip_length < 20 || ip_total_length < ip_length + 8 ||
        length < ethernet_length + ip_length + 8) return -1;
    if (packet[ethernet_length + 9] != 17) return 0;
    udp_offset = ethernet_length + ip_length;
    udp_length = df_read_be16(packet + udp_offset + 4);
    if (udp_length < 8 || udp_length > ip_total_length - ip_length) return -1;
    prefix.source_port = df_read_be16(packet + udp_offset);
    prefix.destination_port = df_read_be16(packet + udp_offset + 2);
    prefix.payload_offset = udp_offset + 8;
    prefix.declared_payload_length = udp_length - 8;
    prefix.captured_payload_length = length - prefix.payload_offset;
    if (prefix.captured_payload_length > prefix.declared_payload_length)
        prefix.captured_payload_length = prefix.declared_payload_length;
    prefix.payload_complete =
        prefix.captured_payload_length == prefix.declared_payload_length;
    *output = prefix;
    return 1;
}

int df_gvs_extract_control_payload(const uint8_t *packet, size_t length,
                                   const uint8_t **payload, size_t *payload_length) {
    size_t ethernet_length = 14;
    size_t ip_length;
    size_t ip_total_length;
    size_t udp_offset;
    size_t udp_length;
    uint16_t ether_type;
    uint16_t source_port;
    uint16_t destination_port;

    if (packet == NULL || payload == NULL || payload_length == NULL ||
        length < ethernet_length) {
        return -1;
    }
    ether_type = df_read_be16(packet + 12);
    while (ether_type == 0x8100 || ether_type == 0x88a8) {
        if (length < ethernet_length + 4) {
            return -1;
        }
        ether_type = df_read_be16(packet + ethernet_length + 2);
        ethernet_length += 4;
    }
    if (ether_type != 0x0800) {
        return 0;
    }
    if (length < ethernet_length + 20 || (packet[ethernet_length] >> 4) != 4) {
        return -1;
    }
    ip_length = (size_t)(packet[ethernet_length] & 0x0f) * 4;
    ip_total_length = df_read_be16(packet + ethernet_length + 2);
    if (ip_length < 20 || ip_total_length < ip_length + 8 ||
        length < ethernet_length + ip_total_length || packet[ethernet_length + 9] != 17) {
        return -1;
    }
    udp_offset = ethernet_length + ip_length;
    udp_length = df_read_be16(packet + udp_offset + 4);
    if (udp_length < 8 || udp_length > ip_total_length - ip_length) {
        return -1;
    }
    source_port = df_read_be16(packet + udp_offset);
    destination_port = df_read_be16(packet + udp_offset + 2);
    if (source_port != 8300 && destination_port != 8300) {
        return 0;
    }
    *payload = packet + udp_offset + 8;
    *payload_length = udp_length - 8;
    return 1;
}
