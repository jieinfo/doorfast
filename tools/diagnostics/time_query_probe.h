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
enum tq_cancel_reason {
    TQ_CANCEL_NONE,
    TQ_CANCEL_OWNER_CONFLICT,
    TQ_CANCEL_CALL_OWNER_CONFLICT,
    TQ_CANCEL_STATUS_READ_FAILED,
    TQ_CANCEL_INCOMING_CALL,
    TQ_CANCEL_LOCAL_HANGUP,
    TQ_CANCEL_PEER_HANGUP,
    TQ_CANCEL_CAPTURE_FAILED,
    TQ_CANCEL_INTERRUPTED,
    TQ_CANCEL_OBSERVATION_TIMEOUT,
};
struct tq_probe {
    struct tq_config config;
    uint64_t generation, call_ms, ack_ms;
    unsigned attempts, sent, preparation_stops;
    bool call_seen, ack_seen, cancelled;
    enum tq_cancel_reason cancel_reason;
    uint8_t ethernet_ip_udp[42];
};
typedef int (*tq_emit_fn)(const uint8_t *, size_t, void *);
void tq_init(struct tq_probe *, const struct tq_config *);
/* Preserve the first cancellation cause; a later event cannot revive it. */
void tq_cancel(struct tq_probe *, enum tq_cancel_reason);
const char *tq_cancel_reason_name(enum tq_cancel_reason);
bool tq_check_owner(struct tq_probe *, const struct tq_owner *);
/* Input is an Ethernet packet, never a UDP socket binding or saved replay. */
int tq_observe(struct tq_probe *, const uint8_t *, size_t, uint64_t now_ms,
               unsigned age_ms, const struct tq_owner *);
bool tq_may_send(const struct tq_probe *, const struct tq_owner *, uint64_t now_ms);
int tq_send(struct tq_probe *, const struct tq_owner *, uint64_t now_ms,
            tq_emit_fn, void *);
#endif
