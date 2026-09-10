#ifndef DF_EVIDENCE_METADATA_H
#define DF_EVIDENCE_METADATA_H
#include "evidence_log.h"
#include "capture.h"
int df_evidence_metadata_append(struct df_evidence_log *,
    const struct df_capture_record *, const char *, uint64_t);
#endif
