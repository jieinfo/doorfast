#ifndef DOORFAST_GVS_IDENTITY_H
#define DOORFAST_GVS_IDENTITY_H

#include <stdint.h>

#include "doorfast.h"
#include "gvs_frame.h"

#define DF_GVS_IPV4_TEXT_SIZE 16
#define DF_GVS_INDOOR_PEER_COUNT 5

int df_gvs_identity_parse(const char *address, uint8_t identity[6]);
int df_gvs_identity_unicast_ip(const uint8_t identity[6],
                               char ip[DF_GVS_IPV4_TEXT_SIZE]);
int df_gvs_identity_multicast_ip(const uint8_t identity[6],
                                 char ip[DF_GVS_IPV4_TEXT_SIZE]);
int df_gvs_identity_indoor_peers(
    const uint8_t identity[6],
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6]);
int df_gvs_frame_is_for_identity(const struct df_gvs_frame *frame,
                                 const uint8_t identity[6]);

#endif
