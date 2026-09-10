#ifndef DF_PCAP_RING_H
#define DF_PCAP_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "capture.h"

#define DF_PCAP_RING_DIRECTORY_MAX 512
#define DF_PCAP_RING_PREFIX_MAX 64

struct df_pcap_ring_config {
    const char *directory;
    const char *prefix;
    uint32_t segment_count;
    uint64_t segment_bytes;
    uint32_t snaplen;
};

struct df_pcap_ring_status {
    uint32_t active_slot;
    uint32_t completed_segments;
    uint64_t bytes_written;
};

struct df_pcap_ring {
    bool initialized;
    bool has_records;
    char directory[DF_PCAP_RING_DIRECTORY_MAX];
    char prefix[DF_PCAP_RING_PREFIX_MAX];
    uint32_t segment_count;
    uint64_t segment_bytes;
    uint32_t snaplen;
    uint32_t active_slot;
    uint32_t completed_segments;
    uint64_t current_bytes;
    uint64_t stored_bytes;
    uint64_t replaced_bytes;
    FILE *file;
};

int df_pcap_ring_init(struct df_pcap_ring *ring,
                      const struct df_pcap_ring_config *config);
int df_pcap_ring_write(struct df_pcap_ring *ring,
                       const struct df_capture_record *record);
int df_pcap_ring_status(const struct df_pcap_ring *ring,
                        struct df_pcap_ring_status *status);
int df_pcap_ring_close(struct df_pcap_ring *ring);

#endif
