#ifndef DOORFAST_GVS_REPLAY_H
#define DOORFAST_GVS_REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_session.h"

struct df_gvs_replay_stats {
    size_t packets_seen;
    size_t control_datagrams;
    size_t accepted_calls;
    size_t invalid_datagrams;
    size_t talking_transitions;
    size_t rejected_pick_frames;
    size_t ended_transitions;
    size_t observed_hangups;
    size_t preempted_sessions;
    size_t calls_started;
    size_t timed_out_sessions;
    size_t invalid_timestamps;
    size_t time_sync_updates;
    size_t rejected_time_sync;
    size_t simulated_frames, simulated_disconnects, handshake_received;
};

int df_gvs_replay_file(const char *path, const uint8_t identity[6],
                       struct df_gvs_session *session,
                       struct df_gvs_replay_stats *stats);
int df_gvs_replay_handshake_file(const char *path, const uint8_t identity[6],
    struct df_gvs_session *session, struct df_gvs_replay_stats *stats);

#endif
