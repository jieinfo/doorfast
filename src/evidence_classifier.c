#include "evidence_classifier.h"

#include <string.h>

#include "event.h"
#include "gvs_frame.h"
#include "gvs_packet.h"

static bool df_evidence_port(uint16_t port) {
    return port == 8300 || port == 8302 || port == 8303 || port == 8304;
}

int df_evidence_classify(const struct df_capture_record *record,
                         struct df_evidence_classification *output) {
    struct df_evidence_classification classification = {0};
    struct df_udp_prefix prefix;
    const uint8_t *payload = NULL;
    size_t payload_length = 0;
    struct df_gvs_frame frame;
    struct df_event event;
    int inspected;

    if (output != NULL) *output = classification;
    if (record == NULL || output == NULL || record->data == NULL ||
        record->captured_length == 0 ||
        record->captured_length > record->original_length) return DF_ERR_INVALID;
    inspected = df_gvs_inspect_udp_prefix(record->data,
                                          record->captured_length, &prefix);
    if (inspected < 0) return DF_ERR_INVALID;
    if (inspected == 0) return DF_OK;
    classification.source_port = prefix.source_port;
    classification.destination_port = prefix.destination_port;
    classification.recent = df_evidence_port(prefix.source_port) ||
                            df_evidence_port(prefix.destination_port);
    if ((prefix.source_port == 8300 || prefix.destination_port == 8300) &&
        prefix.payload_complete &&
        df_gvs_extract_control_payload(record->data, record->captured_length,
                                       &payload, &payload_length) == 1 &&
        df_gvs_frame_parse(payload, payload_length, &frame, &event) == DF_OK) {
        classification.valid_control = true;
        classification.family = frame.family;
        classification.opcode = frame.opcode;
        classification.control_length = payload_length;
    }
    *output = classification;
    return DF_OK;
}
