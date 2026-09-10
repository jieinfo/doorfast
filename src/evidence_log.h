#ifndef DF_EVIDENCE_LOG_H
#define DF_EVIDENCE_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "doorfast.h"

#define DF_EVIDENCE_LOG_DIRECTORY_MAX 512
#define DF_EVIDENCE_LOG_PREFIX_MAX 64

struct df_evidence_log_config {
    const char *directory;
    const char *prefix;
    uint32_t segment_count;
    uint64_t segment_bytes;
};

struct df_evidence_log_record {
    uint64_t wall_seconds;
    uint32_t wall_microseconds;
    uint64_t monotonic_ms;
    const char *interface_name;
    const char *source_mac;
    const char *destination_mac;
    const char *source_ip;
    const char *destination_ip;
    uint16_t source_port;
    uint16_t destination_port;
    const char *source_gvs;
    const char *destination_gvs;
    uint16_t family;
    uint16_t opcode;
    uint32_t length;
    uint64_t session_generation;
    const char *health_state;
};

struct df_evidence_log_status {
    uint32_t active_slot;
    uint32_t completed_segments;
    uint64_t bytes_written;
};

struct df_evidence_log {
    bool initialized;
    bool has_records;
    char directory[DF_EVIDENCE_LOG_DIRECTORY_MAX];
    char prefix[DF_EVIDENCE_LOG_PREFIX_MAX];
    uint32_t segment_count;
    uint64_t segment_bytes;
    uint32_t active_slot;
    uint32_t completed_segments;
    uint64_t current_bytes;
    uint64_t stored_bytes;
    uint64_t replaced_bytes;
    FILE *file;
};

int df_evidence_log_open(struct df_evidence_log *log,
                         const struct df_evidence_log_config *config);
int df_evidence_log_append(struct df_evidence_log *log,
                           const struct df_evidence_log_record *record);
int df_evidence_log_status(const struct df_evidence_log *log,
                           struct df_evidence_log_status *status);
int df_evidence_log_close(struct df_evidence_log *log);

#endif
