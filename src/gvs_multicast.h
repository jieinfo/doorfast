#ifndef DOORFAST_GVS_MULTICAST_H
#define DOORFAST_GVS_MULTICAST_H

#include <stdbool.h>
#include <stdint.h>

#include "gvs_identity.h"

#define DF_GVS_MULTICAST_PORT 8300U

struct df_gvs_multicast {
    int fd;
    uint16_t port;
    char group[DF_GVS_IPV4_TEXT_SIZE];
    char local_address[DF_GVS_IPV4_TEXT_SIZE];
    bool joined;
};

int df_gvs_multicast_prepare(struct df_gvs_multicast *, const uint8_t [6],
                             const char *);
int df_gvs_multicast_open(struct df_gvs_multicast *, const uint8_t [6],
                          const char *);
void df_gvs_multicast_close(struct df_gvs_multicast *);

#endif
