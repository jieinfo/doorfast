#ifndef DF_EVIDENCE_RECORDER_H
#define DF_EVIDENCE_RECORDER_H
#include "pcap_ring.h"

enum df_recorder_state { DF_RECORDER_RECORDING, DF_RECORDER_SPACE_GUARD,
                         DF_RECORDER_IO_ERROR };
typedef int (*df_recorder_space_fn)(void *, uint64_t *);
struct df_evidence_recorder {
    struct df_pcap_ring *recent, *control;
    df_recorder_space_fn space;
    void *space_context;
    uint64_t reserve_bytes, available_bytes;
    uint64_t packets_seen, recent_packets, control_packets, invalid_packets;
    uint64_t last_packet_wall_seconds, last_rotation_wall_seconds;
    enum df_recorder_state state;
};
/* Rings remain caller-owned. A guarded or failed recorder stays stopped until
 * explicitly initialized again, so recovery cannot silently resume writes. */
int df_evidence_recorder_init(struct df_evidence_recorder *,
    struct df_pcap_ring *, struct df_pcap_ring *, uint64_t,
    df_recorder_space_fn, void *);
int df_evidence_recorder_accept(struct df_evidence_recorder *,
    const struct df_capture_record *);
#endif
