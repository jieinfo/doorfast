#include "gvs_audio_tx.h"

#include <limits.h>
#include <string.h>

#include "doorfast.h"
#include "g711_alaw.h"
#include "gvs_media.h"

static bool address_valid(const uint8_t address[6])
{
    static const uint8_t zero[6] = {0};

    return address != NULL && memcmp(address, zero, sizeof(zero)) != 0;
}

int df_gvs_audio_tx_init(struct df_gvs_audio_tx *tx,
                         uint16_t initial_sequence,
                         df_gvs_audio_tx_emit_fn emit, void *context)
{
    if (tx == NULL || emit == NULL) {
        return DF_ERR_INVALID;
    }
    memset(tx, 0, sizeof(*tx));
    tx->next_sequence = initial_sequence;
    tx->emit = emit;
    tx->emit_context = context;
    tx->initialized = true;
    return DF_OK;
}

int df_gvs_audio_tx_start(struct df_gvs_audio_tx *tx,
                          const struct df_gvs_session *session,
                          uint64_t generation, const uint8_t source[6],
                          uint64_t now_ms)
{
    if (tx == NULL || session == NULL || !tx->initialized || tx->active ||
        session->state != DF_GVS_TALKING || generation == 0U ||
        generation != session->generation || now_ms < tx->last_now_ms ||
        !address_valid(session->peer) || !address_valid(source)) {
        return DF_ERR_INVALID;
    }
    memcpy(tx->destination, session->peer, sizeof(tx->destination));
    memcpy(tx->source, source, sizeof(tx->source));
    tx->generation = generation;
    tx->next_send_ms = now_ms;
    tx->last_now_ms = now_ms;
    tx->active = true;
    return DF_OK;
}

int df_gvs_audio_tx_submit_pcm(struct df_gvs_audio_tx *tx,
                               uint64_t generation, const int16_t *pcm,
                               size_t sample_count, uint64_t now_ms)
{
    uint8_t payload[DF_GVS_AUDIO_TX_SAMPLES];
    uint8_t frame[DF_GVS_AUDIO_HEADER_LEN + DF_GVS_AUDIO_TX_SAMPLES];
    size_t frame_length = 0;

    if (tx == NULL || !tx->initialized || !tx->active || pcm == NULL ||
        generation == 0U || generation != tx->generation ||
        sample_count != DF_GVS_AUDIO_TX_SAMPLES ||
        now_ms < tx->last_now_ms || now_ms < tx->next_send_ms ||
        now_ms > UINT64_MAX - DF_GVS_AUDIO_TX_INTERVAL_MS ||
        df_g711_alaw_encode(pcm, sample_count, payload, sizeof(payload)) != 0 ||
        df_gvs_serialize_audio(tx->destination, tx->source,
            tx->next_sequence, payload, sizeof(payload), frame, sizeof(frame),
            &frame_length) != 0) {
        return DF_ERR_INVALID;
    }
    if (tx->emit(frame, frame_length, tx->emit_context) != DF_OK) {
        if (tx->packets_failed < UINT64_MAX) {
            tx->packets_failed++;
        }
        return DF_ERR_IO;
    }
    tx->next_sequence++;
    tx->next_send_ms = now_ms + DF_GVS_AUDIO_TX_INTERVAL_MS;
    tx->last_now_ms = now_ms;
    if (tx->packets_sent < UINT64_MAX) {
        tx->packets_sent++;
    }
    return DF_OK;
}

int df_gvs_audio_tx_stop(struct df_gvs_audio_tx *tx, uint64_t generation,
                         uint64_t now_ms)
{
    if (tx == NULL || !tx->initialized || !tx->active || generation == 0U ||
        generation != tx->generation || now_ms < tx->last_now_ms) {
        return DF_ERR_INVALID;
    }
    memset(tx->destination, 0, sizeof(tx->destination));
    memset(tx->source, 0, sizeof(tx->source));
    tx->generation = 0;
    tx->next_send_ms = 0;
    tx->last_now_ms = now_ms;
    tx->active = false;
    return DF_OK;
}

int df_gvs_audio_tx_read_status(const struct df_gvs_audio_tx *tx,
                                struct df_gvs_audio_tx_status *status)
{
    if (tx == NULL || status == NULL || !tx->initialized) {
        return DF_ERR_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->active = tx->active;
    status->generation = tx->generation;
    status->packets_sent = tx->packets_sent;
    status->packets_failed = tx->packets_failed;
    status->next_send_ms = tx->next_send_ms;
    status->next_sequence = tx->next_sequence;
    return DF_OK;
}
