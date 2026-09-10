#ifndef DOORFAST_GVS_PACKET_H
#define DOORFAST_GVS_PACKET_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct df_udp_prefix {
    uint16_t source_port;
    uint16_t destination_port;
    size_t payload_offset;
    size_t captured_payload_length;
    size_t declared_payload_length;
    bool payload_complete;
};

/* Returns 1 for a complete UDP header, 0 for unrelated traffic, and -1 for
 * malformed or header-truncated Ethernet/IPv4/UDP input. */
int df_gvs_inspect_udp_prefix(const uint8_t *packet, size_t length,
                              struct df_udp_prefix *prefix);

/* Returns 1 for a GVS control datagram, 0 for an unrelated packet, -1 for a
 * malformed Ethernet/IPv4/UDP packet. */
int df_gvs_extract_control_payload(const uint8_t *packet, size_t length,
                                   const uint8_t **payload, size_t *payload_length);

#endif
