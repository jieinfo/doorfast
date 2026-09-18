#ifndef DOORFAST_GVS_STATION_DISCOVERY_H
#define DOORFAST_GVS_STATION_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_GVS_STATION_CANDIDATE_CAPACITY 64U
#define DF_GVS_STATION_SCAN_PAYLOAD_SIZE 4U

struct df_gvs_station_scan_action {
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t family;
    uint8_t opcode;
    uint8_t payload[DF_GVS_STATION_SCAN_PAYLOAD_SIZE];
    size_t payload_length;
};

struct df_gvs_station_scan {
    uint8_t identity[6];
    uint64_t started_ms;
    uint64_t last_now_ms;
    unsigned emitted;
    bool active;
};

struct df_gvs_station_candidate {
    uint8_t logical_address[6];
    uint32_t ipv4;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint64_t reply_count;
    bool valid;
};

struct df_gvs_station_discovery {
    struct df_gvs_station_candidate candidates[
        DF_GVS_STATION_CANDIDATE_CAPACITY];
    size_t count;
};

int df_gvs_station_scan_start(struct df_gvs_station_scan *,
    const uint8_t identity[6], uint64_t now_ms);
int df_gvs_station_scan_next(struct df_gvs_station_scan *, uint64_t now_ms,
    struct df_gvs_station_scan_action *);
int df_gvs_station_discovery_observe(struct df_gvs_station_discovery *,
    const uint8_t *packet, size_t packet_length,
    const uint8_t identity[6], uint64_t now_ms);

#endif
