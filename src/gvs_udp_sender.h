#ifndef DOORFAST_GVS_UDP_SENDER_H
#define DOORFAST_GVS_UDP_SENDER_H

#include <netinet/in.h>
#include "gvs_access.h"
#include "gvs_call_dispatch.h"
#include "gvs_elevator.h"
#include "gvs_incoming_reply.h"
#include "gvs_presence.h"

#define DF_GVS_OBSERVED_ROUTE_CAPACITY 8U

struct df_gvs_observed_route {
    uint8_t peer[6];
    uint32_t ipv4;
    bool valid;
};

struct df_gvs_udp_sender {
    int fd;
    struct sockaddr_in peer;
    df_gvs_header_provider_fn provide_fields;
    void *fields_context;
    unsigned sent;
    unsigned failed;
    struct df_gvs_observed_route observed_routes[
        DF_GVS_OBSERVED_ROUTE_CAPACITY];
    size_t next_observed_route;
};

int df_gvs_udp_sender_open(struct df_gvs_udp_sender *, const char *, uint16_t,
    df_gvs_header_provider_fn, void *);
void df_gvs_udp_sender_close(struct df_gvs_udp_sender *);
enum df_gvs_send_attempt_result df_gvs_udp_send_attempt(
    const struct df_gvs_call_command *, unsigned, uint64_t, void *);
struct df_gvs_udp_presence_context {
    struct df_gvs_udp_sender *sender;
    const uint8_t *source;
    uint16_t sync_version;
};
int df_gvs_udp_presence_emit(const struct df_gvs_presence_action *, void *);
int df_gvs_udp_incoming_reply_emit(const struct df_gvs_incoming_reply *,
    struct df_gvs_udp_sender *);
int df_gvs_udp_sender_observe_peer(struct df_gvs_udp_sender *,
    const uint8_t *, size_t, const uint8_t [6]);

int df_gvs_udp_access_emit(const struct df_gvs_access_request *, void *);
int df_gvs_udp_elevator_emit(const struct df_gvs_elevator_request *, void *);
int df_gvs_udp_audio_emit(const uint8_t *, size_t, void *);

#endif
