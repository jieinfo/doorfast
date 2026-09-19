#include "gvs_station_discovery.h"

#include "gvs_frame.h"
#include "gvs_identity.h"
#include "gvs_packet.h"
#include "gvs_station.h"

#include <arpa/inet.h>
#include <limits.h>
#include <string.h>

#define DF_GVS_STATION_SCAN_INTERVAL_MS 500U
#define DF_GVS_STATION_SCAN_COUNT 3U
#define DF_GVS_STATION_REPLY_SIZE 42U

static bool df_gvs_station_ipv4_is_unicast(uint32_t ipv4) {
    uint32_t host = ntohl(ipv4);
    unsigned first_octet = (unsigned)(host >> 24U);

    return host != 0U && host != UINT32_MAX && first_octet > 0U &&
        first_octet < 224U;
}

static bool df_gvs_station_identity_is_valid(const uint8_t identity[6]) {
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6];

    return identity != NULL &&
        df_gvs_identity_indoor_peers(identity, peers) == DF_OK;
}

int df_gvs_station_scan_start(struct df_gvs_station_scan *scan,
    const uint8_t identity[6], uint64_t now_ms) {
    struct df_gvs_station_scan next = {0};

    if (scan == NULL ||
        now_ms > UINT64_MAX -
            (DF_GVS_STATION_SCAN_INTERVAL_MS *
             (DF_GVS_STATION_SCAN_COUNT - 1U)) ||
        !df_gvs_station_identity_is_valid(identity))
        return DF_ERR_INVALID;
    memcpy(next.identity, identity, sizeof(next.identity));
    next.started_ms = now_ms;
    next.last_now_ms = now_ms;
    next.active = true;
    *scan = next;
    return DF_OK;
}

int df_gvs_station_scan_next(struct df_gvs_station_scan *scan,
    uint64_t now_ms, struct df_gvs_station_scan_action *action) {
    struct df_gvs_station_scan_action next = {0};
    uint64_t due_ms;

    if (scan == NULL || action == NULL || now_ms < scan->last_now_ms)
        return -1;
    scan->last_now_ms = now_ms;
    if (!scan->active) return 0;
    due_ms = scan->started_ms +
        (uint64_t)scan->emitted * DF_GVS_STATION_SCAN_INTERVAL_MS;
    if (now_ms < due_ms) return 0;

    next.destination[0] = 0x32U;
    next.destination[1] = scan->identity[1];
    next.destination[2] = scan->identity[2];
    memset(next.destination + 3U, 0xff, 3U);
    memcpy(next.source, scan->identity, sizeof(next.source));
    next.family = 0x07U;
    next.opcode = 0x06U;
    memcpy(next.payload,
        (const uint8_t[]){0x02U, 0x00U, 0x00U, 0x01U},
        sizeof(next.payload));
    next.payload_length = sizeof(next.payload);
    *action = next;
    scan->emitted++;
    if (scan->emitted == DF_GVS_STATION_SCAN_COUNT) scan->active = false;
    return 1;
}

static struct df_gvs_station_candidate *df_gvs_station_candidate_find(
    struct df_gvs_station_discovery *discovery,
    const uint8_t logical_address[6]) {
    size_t index;

    for (index = 0U; index < DF_GVS_STATION_CANDIDATE_CAPACITY; index++) {
        struct df_gvs_station_candidate *candidate =
            &discovery->candidates[index];

        if (candidate->valid && memcmp(candidate->logical_address,
                logical_address, sizeof(candidate->logical_address)) == 0)
            return candidate;
    }
    return NULL;
}

static struct df_gvs_station_candidate *df_gvs_station_candidate_slot(
    struct df_gvs_station_discovery *discovery) {
    struct df_gvs_station_candidate *least_recent = NULL;
    size_t index;

    for (index = 0U; index < DF_GVS_STATION_CANDIDATE_CAPACITY; index++) {
        struct df_gvs_station_candidate *candidate =
            &discovery->candidates[index];

        if (!candidate->valid) return candidate;
        if (least_recent == NULL ||
            candidate->last_seen_ms < least_recent->last_seen_ms)
            least_recent = candidate;
    }
    return least_recent;
}

int df_gvs_station_discovery_observe(struct df_gvs_station_discovery *discovery,
    const uint8_t *packet, size_t packet_length,
    const uint8_t identity[6], uint64_t now_ms) {
    struct df_udp_prefix prefix;
    struct df_gvs_frame frame;
    struct df_event event;
    struct df_gvs_station_candidate *candidate;

    if (discovery == NULL || packet == NULL ||
        !df_gvs_station_identity_is_valid(identity) ||
        df_gvs_inspect_udp_prefix(packet, packet_length, &prefix) != 1 ||
        !prefix.payload_complete || prefix.destination_port != 8300U ||
        prefix.declared_payload_length != DF_GVS_STATION_REPLY_SIZE ||
        !df_gvs_station_ipv4_is_unicast(prefix.source_ipv4) ||
        df_gvs_frame_parse(packet + prefix.payload_offset,
            prefix.declared_payload_length, &frame, &event) != DF_OK ||
        frame.family != 0x07U || frame.opcode != 0x86U ||
        frame.payload_length != 0U ||
        memcmp(frame.destination, identity, 6U) != 0 ||
        df_gvs_station_validate(frame.source) != DF_OK ||
        frame.source[1] != identity[1] || frame.source[2] != identity[2])
        return DF_ERR_INVALID;

    candidate = df_gvs_station_candidate_find(discovery, frame.source);
    if (candidate != NULL) {
        if (now_ms < candidate->last_seen_ms) return DF_ERR_INVALID;
        candidate->last_seen_ms = now_ms;
        candidate->ipv4 = prefix.source_ipv4;
        if (candidate->reply_count < UINT64_MAX) candidate->reply_count++;
        return DF_OK;
    }

    candidate = df_gvs_station_candidate_slot(discovery);
    if (candidate == NULL) return DF_ERR_INVALID;
    memset(candidate, 0, sizeof(*candidate));
    memcpy(candidate->logical_address, frame.source,
        sizeof(candidate->logical_address));
    candidate->ipv4 = prefix.source_ipv4;
    candidate->first_seen_ms = now_ms;
    candidate->last_seen_ms = now_ms;
    candidate->reply_count = 1U;
    candidate->valid = true;
    if (discovery->count < DF_GVS_STATION_CANDIDATE_CAPACITY)
        discovery->count++;
    return DF_OK;
}
