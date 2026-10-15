#ifndef DOORFAST_TIME_QUERY_PROBE_H
#define DOORFAST_TIME_QUERY_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tq_owner {
    char runtime[17], station[64], stage[16], native_station[64], native_state[16];
    uint64_t operation, generation, native_generation;
    bool media_empty;
};
struct tq_config {
    char runtime[17], station[64];
    uint64_t operation;
    uint32_t local_ip, peer_ip;
    uint8_t local[6], peer[6];
    bool send;
};
struct tq_probe {
    struct tq_config config;
    uint64_t generation, call_ms, ack_ms;
    unsigned attempts, sent;
    bool call_seen, ack_seen, cancelled;
    uint8_t ethernet_ip_udp[42];
};
typedef int (*tq_emit_fn)(const uint8_t *, size_t, void *);
void tq_init(struct tq_probe *, const struct tq_config *);
/* Input is an Ethernet packet, never a UDP socket binding or saved replay. */
int tq_observe(struct tq_probe *, const uint8_t *, size_t, uint64_t now_ms,
               unsigned age_ms, const struct tq_owner *);
bool tq_may_send(const struct tq_probe *, const struct tq_owner *, uint64_t now_ms);
int tq_send(struct tq_probe *, const struct tq_owner *, uint64_t now_ms,
            tq_emit_fn, void *);
#endif
