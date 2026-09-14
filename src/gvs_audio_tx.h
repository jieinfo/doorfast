#ifndef DOORFAST_GVS_AUDIO_TX_H
#define DOORFAST_GVS_AUDIO_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_session.h"

#define DF_GVS_AUDIO_TX_SAMPLE_RATE 8000U
#define DF_GVS_AUDIO_TX_INTERVAL_MS 20U
#define DF_GVS_AUDIO_TX_SAMPLES 160U

typedef int (*df_gvs_audio_tx_emit_fn)(const uint8_t *, size_t, void *);

struct df_gvs_audio_tx_status {
    bool active;
    uint64_t generation;
    uint64_t packets_sent;
    uint64_t packets_failed;
    uint64_t next_send_ms;
    uint16_t next_sequence;
};

struct df_gvs_audio_tx {
    uint8_t destination[6];
    uint8_t source[6];
    uint64_t generation;
    uint64_t packets_sent;
    uint64_t packets_failed;
    uint64_t next_send_ms;
    uint64_t last_now_ms;
    uint16_t next_sequence;
    df_gvs_audio_tx_emit_fn emit;
    void *emit_context;
    bool initialized;
    bool active;
};

int df_gvs_audio_tx_init(struct df_gvs_audio_tx *, uint16_t,
    df_gvs_audio_tx_emit_fn, void *);
int df_gvs_audio_tx_start(struct df_gvs_audio_tx *,
    const struct df_gvs_session *, uint64_t, const uint8_t [6], uint64_t);
int df_gvs_audio_tx_submit_pcm(struct df_gvs_audio_tx *, uint64_t,
    const int16_t *, size_t, uint64_t);
int df_gvs_audio_tx_stop(struct df_gvs_audio_tx *, uint64_t, uint64_t);
int df_gvs_audio_tx_sync(struct df_gvs_audio_tx *,
    const struct df_gvs_session *, const uint8_t [6], uint64_t);
int df_gvs_audio_tx_read_status(const struct df_gvs_audio_tx *,
    struct df_gvs_audio_tx_status *);

#endif
