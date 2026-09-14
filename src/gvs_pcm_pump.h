#ifndef DOORFAST_GVS_PCM_PUMP_H
#define DOORFAST_GVS_PCM_PUMP_H

#include <stdint.h>

#include "gvs_audio_tx.h"
#include "gvs_pcm_ingress.h"

#define DF_GVS_PCM_PUMP_MAX_DRAIN 32U

struct df_gvs_pcm_pump_result {
    unsigned received;
    unsigned invalid;
    unsigned stale;
    unsigned transmitted;
    unsigned send_failed;
};

int df_gvs_pcm_pump(struct df_gvs_pcm_ingress *, struct df_gvs_audio_tx *,
    uint64_t, struct df_gvs_pcm_pump_result *);

#endif
