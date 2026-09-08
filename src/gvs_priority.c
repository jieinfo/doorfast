#include "gvs_priority.h"
#include <string.h>

int df_gvs_call_category(const uint8_t peer[6], const uint8_t local[6]) {
    if (peer == NULL || local == NULL) return -1;
    switch (peer[0]) {
    case 0x12:
    case 0x31:
        return 1;
    case 0x13:
    case 0x32:
        return 0;
    case 0x61:
        return memcmp(peer, local, 5) == 0 ? 4 : 3;
    case 0x62:
        return 7;
    default:
        return -1;
    }
}

static int priority_rank(int category) {
    switch (category) {
    case 0:
    case 1:
    case 7:
        return 2;
    case 3:
        return 1;
    case 4:
        return 0;
    default:
        return -1;
    }
}

enum df_gvs_priority_decision df_gvs_priority_compare(int current, int incoming) {
    int current_rank = priority_rank(current);
    int incoming_rank = priority_rank(incoming);
    if (current_rank < 0 || incoming_rank < 0) {
        return DF_GVS_PRIORITY_UNSUPPORTED;
    }
    return incoming_rank > current_rank ? DF_GVS_PRIORITY_PREEMPT : DF_GVS_PRIORITY_KEEP;
}
