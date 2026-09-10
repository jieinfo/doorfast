#ifndef DF_EVIDENCE_CLASSIFIER_H
#define DF_EVIDENCE_CLASSIFIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "capture.h"

struct df_evidence_classification {
    bool recent;
    bool valid_control;
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t family;
    uint8_t opcode;
    size_t control_length;
};

int df_evidence_classify(const struct df_capture_record *record,
                         struct df_evidence_classification *classification);

#endif
