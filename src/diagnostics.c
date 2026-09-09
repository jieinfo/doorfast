#include "diagnostics.h"

#include <arpa/inet.h>
#include <stddef.h>

static uint32_t df_network_mask(uint8_t prefix_length) {
    if (prefix_length == 0) {
        return 0;
    }
    return UINT32_MAX << (32 - prefix_length);
}

enum df_overlap_state df_diagnostics_overlap(const struct df_network_info *doorfast,
                                             const struct df_network_info *entry_network) {
    struct in_addr doorfast_address;
    struct in_addr entry_address;
    uint32_t mask;

    if (doorfast == NULL || entry_network == NULL || doorfast->address == NULL ||
        entry_network->address == NULL || doorfast->prefix_length > 32 ||
        entry_network->prefix_length > 32 ||
        inet_pton(AF_INET, doorfast->address, &doorfast_address) != 1 ||
        inet_pton(AF_INET, entry_network->address, &entry_address) != 1) {
        return DF_OVERLAP_INVALID;
    }
    mask = df_network_mask(doorfast->prefix_length < entry_network->prefix_length
                               ? doorfast->prefix_length
                               : entry_network->prefix_length);
    return (ntohl(doorfast_address.s_addr) & mask) == (ntohl(entry_address.s_addr) & mask)
               ? DF_OVERLAP_WARNING
               : DF_OVERLAP_NONE;
}
