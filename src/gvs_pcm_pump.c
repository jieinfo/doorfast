#include "gvs_pcm_pump.h"

#include <string.h>

#include "doorfast.h"

int df_gvs_pcm_pump(struct df_gvs_pcm_ingress *ingress,
                    struct df_gvs_audio_tx *tx, uint64_t now_ms,
                    struct df_gvs_pcm_pump_result *result)
{
    struct df_gvs_audio_tx_status status;
    int16_t pcm[DF_GVS_AUDIO_TX_SAMPLES];
    unsigned index;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (ingress == NULL || tx == NULL || result == NULL ||
        df_gvs_audio_tx_read_status(tx, &status) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (status.active && now_ms < status.next_send_ms) {
        return DF_OK;
    }
    for (index = 0; index < DF_GVS_PCM_PUMP_MAX_DRAIN; index++) {
        uint64_t generation = 0;
        int received = df_gvs_pcm_ingress_receive(
            ingress, &generation, pcm, DF_GVS_AUDIO_TX_SAMPLES);

        if (received == DF_GVS_PCM_INGRESS_EMPTY) {
            return DF_OK;
        }
        if (received == DF_GVS_PCM_INGRESS_ERROR) {
            return DF_ERR_IO;
        }
        result->received++;
        if (received == DF_GVS_PCM_INGRESS_INVALID) {
            result->invalid++;
            continue;
        }
        if (!status.active || generation != status.generation) {
            result->stale++;
            continue;
        }
        received = df_gvs_audio_tx_submit_pcm(
            tx, generation, pcm, DF_GVS_AUDIO_TX_SAMPLES, now_ms);
        if (received == DF_OK) {
            result->transmitted = 1;
            return DF_OK;
        }
        if (received == DF_ERR_IO) {
            result->send_failed = 1;
            return DF_OK;
        }
        result->stale++;
    }
    return DF_OK;
}
