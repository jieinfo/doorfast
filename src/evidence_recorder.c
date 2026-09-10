#include "evidence_recorder.h"
#include "evidence_classifier.h"

int df_evidence_recorder_init(struct df_evidence_recorder *out,
    struct df_pcap_ring *recent, struct df_pcap_ring *control,
    uint64_t reserve, df_recorder_space_fn space, void *context) {
    if (!out || !recent || !control || recent == control || !space || !reserve ||
        !recent->initialized || !control->initialized ||
        !recent->file || !control->file || recent->snaplen != 256 ||
        control->snaplen != 2048) return DF_ERR_INVALID;
    *out = (struct df_evidence_recorder){.recent = recent, .control = control,
        .reserve_bytes = reserve, .space = space, .space_context = context,
        .state = DF_RECORDER_RECORDING};
    return DF_OK;
}

int df_evidence_recorder_accept(struct df_evidence_recorder *r,
    const struct df_capture_record *record) {
    struct df_evidence_classification classification;
    uint64_t needed = 0;
    if (!r || !r->space || !record || !record->data ||
        !record->captured_length ||
        record->captured_length > record->original_length ||
        record->wall_seconds > UINT32_MAX || record->wall_microseconds > 999999)
        return DF_ERR_INVALID;
    if (r->state == DF_RECORDER_IO_ERROR) return DF_ERR_IO;
    if (r->state == DF_RECORDER_SPACE_GUARD) return DF_OK;
    r->packets_seen++;
    if (df_evidence_classify(record, &classification) != DF_OK) {
        r->invalid_packets++;
        return DF_OK;
    }
    if (!classification.recent) return DF_OK;
    /* Include possible new global headers. Never credit space from a future
     * replacement: the old completed file coexists with the active partial. */
    needed = 40 + (record->captured_length < 256 ? record->captured_length : 256);
    if (classification.valid_control)
        needed += 40 + (record->captured_length < 2048 ? record->captured_length : 2048);
    if (r->space(r->space_context, &r->available_bytes) != DF_OK) {
        r->state = DF_RECORDER_IO_ERROR;
        return DF_ERR_IO;
    }
    if (r->available_bytes < r->reserve_bytes ||
        r->available_bytes - r->reserve_bytes < needed) {
        r->state = DF_RECORDER_SPACE_GUARD;
        return DF_OK;
    }
    if (df_pcap_ring_write(r->recent, record) != DF_OK) {
        r->state = DF_RECORDER_IO_ERROR;
        return DF_ERR_IO;
    }
    r->recent_packets++;
    if (classification.valid_control) {
        if (df_pcap_ring_write(r->control, record) != DF_OK) {
            r->state = DF_RECORDER_IO_ERROR;
            return DF_ERR_IO;
        }
        r->control_packets++;
    }
    return DF_OK;
}
