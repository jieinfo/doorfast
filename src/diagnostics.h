#ifndef DOORFAST_DIAGNOSTICS_H
#define DOORFAST_DIAGNOSTICS_H

#include <stdint.h>

enum df_overlap_state {
    DF_OVERLAP_INVALID = 0,
    DF_OVERLAP_NONE,
    DF_OVERLAP_WARNING,
};

struct df_network_info {
    const char *address;
    uint8_t prefix_length;
};

enum df_overlap_state df_diagnostics_overlap(const struct df_network_info *doorfast,
                                             const struct df_network_info *entry_network);

#endif
