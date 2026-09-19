#ifndef DOORFAST_GVS_UDP_SENDER_H
#define DOORFAST_GVS_UDP_SENDER_H

#include <netinet/in.h>
#include "gvs_access.h"
#include "gvs_call_dispatch.h"
#include "gvs_elevator.h"
#include "gvs_incoming_reply.h"
#include "gvs_presence.h"
#include "gvs_reply_queue.h"
#include "gvs_station_discovery.h"
#include "gvs_sync.h"

#define DF_GVS_OBSERVED_ROUTE_CAPACITY 8U
#define DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS 60000U
#define DF_GVS_PREVIEW_ROUTE_CAPACITY 4U

struct df_gvs_observed_route {
    uint8_t peer[6];
    uint32_t ipv4;
    uint64_t observed_ms;
    bool valid;
};

struct df_gvs_preview_route {
    uint8_t peer[6];
    uint32_t ipv4;
    uint64_t observed_ms;
    bool configured;
    bool valid;
};

struct df_gvs_udp_sender {
    int fd;
    struct sockaddr_in peer;
    df_gvs_header_provider_fn provide_fields;
    void *fields_context;
    unsigned sent;
    unsigned failed;
    uint64_t current_ms;
    struct df_gvs_observed_route observed_routes[
        DF_GVS_OBSERVED_ROUTE_CAPACITY];
    size_t next_observed_route;
    struct df_gvs_preview_route preview_routes[
        DF_GVS_PREVIEW_ROUTE_CAPACITY];
    size_t next_preview_route;
};

int df_gvs_udp_sender_open(struct df_gvs_udp_sender *, const char *, uint16_t,
    df_gvs_header_provider_fn, void *);
void df_gvs_udp_sender_close(struct df_gvs_udp_sender *);
enum df_gvs_send_attempt_result df_gvs_udp_send_attempt(
    const struct df_gvs_call_command *, unsigned, uint64_t, void *);
struct df_gvs_udp_presence_context {
    struct df_gvs_udp_sender *sender;
    const uint8_t *source;
    const struct df_gvs_sync_store *store;
    uint16_t sync_version;
    size_t emitted_packets;
};
int df_gvs_udp_presence_emit(const struct df_gvs_presence_action *, void *);
enum df_gvs_send_attempt_result df_gvs_udp_peer_reply_send_attempt(
    const struct df_gvs_reply_queue_entry *, unsigned, uint64_t, void *);
int df_gvs_udp_incoming_reply_emit(const struct df_gvs_incoming_reply *,
    struct df_gvs_udp_sender *);
int df_gvs_udp_sender_observe_peer(struct df_gvs_udp_sender *,
    const uint8_t *, size_t, const uint8_t [6], uint64_t now_ms);
int df_gvs_udp_sender_advance(struct df_gvs_udp_sender *, uint64_t now_ms);
int df_gvs_udp_sender_observe_preview_route(struct df_gvs_udp_sender *,
    const uint8_t *, size_t, const uint8_t [6], uint64_t now_ms);
int df_gvs_udp_sender_set_configured_route(struct df_gvs_udp_sender *,
    const uint8_t peer[6], uint32_t ipv4);
int df_gvs_udp_sender_resolve_preview_route(const struct df_gvs_udp_sender *,
    const uint8_t peer[6], uint64_t now_ms, uint64_t max_age_ms,
    uint32_t *ipv4);
int df_gvs_udp_sender_emit_control(struct df_gvs_udp_sender *,
    const uint8_t destination[6], uint32_t destination_ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t payload_length);
int df_gvs_udp_sender_emit_station_scan(struct df_gvs_udp_sender *,
    const struct df_gvs_station_scan_action *);

int df_gvs_udp_access_emit(const struct df_gvs_access_request *, void *);
int df_gvs_udp_elevator_emit(const struct df_gvs_elevator_request *, void *);
int df_gvs_udp_audio_emit(const uint8_t *, size_t, void *);

#endif
