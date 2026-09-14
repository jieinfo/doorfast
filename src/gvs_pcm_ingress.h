#ifndef DOORFAST_GVS_PCM_INGRESS_H
#define DOORFAST_GVS_PCM_INGRESS_H

#include <stddef.h>
#include <stdint.h>

#include "gvs_audio_tx.h"

#define DF_GVS_PCM_INGRESS_MAGIC_SIZE 8U
#define DF_GVS_PCM_INGRESS_HEADER_SIZE 16U
#define DF_GVS_PCM_INGRESS_PACKET_SIZE \
    (DF_GVS_PCM_INGRESS_HEADER_SIZE + DF_GVS_AUDIO_TX_SAMPLES * 2U)

enum df_gvs_pcm_ingress_result {
    DF_GVS_PCM_INGRESS_ERROR = -1,
    DF_GVS_PCM_INGRESS_EMPTY = 0,
    DF_GVS_PCM_INGRESS_FRAME = 1,
    DF_GVS_PCM_INGRESS_INVALID = 2,
};

struct df_gvs_pcm_ingress {
    int fd;
    char path[108];
};

int df_gvs_pcm_ingress_serialize(uint64_t, const int16_t *, size_t,
    uint8_t *, size_t, size_t *);
int df_gvs_pcm_ingress_open(struct df_gvs_pcm_ingress *, const char *);
int df_gvs_pcm_ingress_receive(struct df_gvs_pcm_ingress *, uint64_t *,
    int16_t *, size_t);
void df_gvs_pcm_ingress_close(struct df_gvs_pcm_ingress *);

#endif
