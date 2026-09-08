#ifndef DOORFAST_GVS_PACKET_H
#define DOORFAST_GVS_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* Returns 1 for a GVS control datagram, 0 for an unrelated packet, -1 for a
 * malformed Ethernet/IPv4/UDP packet. */
int df_gvs_extract_control_payload(const uint8_t *packet, size_t length,
                                   const uint8_t **payload, size_t *payload_length);

#endif
