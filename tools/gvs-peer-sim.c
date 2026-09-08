#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gvs_peer_sim.h"
#include "gvs_receive.h"
#include "gvs_runtime_sync.h"

static int sim_step(struct df_gvs_peer_sim *sim,
                    struct df_gvs_runtime_sync *sync, uint64_t now_ms) {
    const uint8_t *frame;
    size_t length;
    struct df_gvs_runtime_sync_result result;

    if (df_gvs_peer_sim_advance(sim, now_ms) != DF_OK) {
        return DF_ERR_INVALID;
    }
    while (df_gvs_peer_sim_next_frame(sim, &frame, &length) == DF_OK) {
        if (df_gvs_runtime_sync_receive(sync, frame, length, now_ms,
                                        &result) != DF_OK ||
            result.rejected) {
            return DF_ERR_IO;
        }
    }
    if (df_gvs_runtime_sync_tick(sync, now_ms, df_gvs_peer_sim_emit, sim) !=
        DF_OK) {
        return DF_ERR_IO;
    }
    while (df_gvs_peer_sim_next_frame(sim, &frame, &length) == DF_OK) {
        if (df_gvs_runtime_sync_receive(sync, frame, length, now_ms,
                                        &result) != DF_OK ||
            result.rejected) {
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

static const char *session_name(enum df_gvs_session_state state) {
    switch (state) {
    case DF_GVS_IDLE: return "idle";
    case DF_GVS_PREVIEW: return "preview";
    case DF_GVS_RINGING: return "ringing";
    case DF_GVS_TALKING: return "talking";
    case DF_GVS_ENDED: return "ended";
    default: return NULL;
    }
}

static int status_snapshot(const struct df_gvs_runtime_sync *sync,
                           struct df_gvs_runtime_sync_status *status,
                           const char **phase, const char **role) {
    if (df_gvs_runtime_sync_status(sync, status) != DF_OK) {
        return DF_ERR_INVALID;
    }
    *phase = df_gvs_runtime_sync_phase_name(status->phase);
    *role = df_gvs_runtime_sync_role_name(status->role);
    return *phase != NULL && *role != NULL ? DF_OK : DF_ERR_INVALID;
}

static int print_election(const struct df_gvs_runtime_sync *sync,
                          const struct df_gvs_peer_sim *sim, uint64_t now_ms,
                          bool evidence_gap) {
    struct df_gvs_runtime_sync_status status;
    const char *phase;
    const char *role;

    if (status_snapshot(sync, &status, &phase, &role) != DF_OK) {
        return DF_ERR_INVALID;
    }
    printf("{\"time_ms\":%llu,\"event\":\"election_complete\","
           "\"phase\":\"%s\",\"role\":\"%s\",\"version\":%u,"
           "\"online_peers\":%zu,\"peer_presence_evidence_gap\":%s,"
           "\"sync_ask_actions\":%zu,\"version_ask_actions\":%zu}\n",
           (unsigned long long)now_ms, phase, role,
           (unsigned)status.sync_version, status.online_peers,
           evidence_gap ? "true" : "false",
           df_gvs_peer_sim_action_count(
               sim, DF_GVS_PRESENCE_SYNC_ASK_ACTION),
           df_gvs_peer_sim_action_count(
               sim, DF_GVS_PRESENCE_SYNC_VERSION_ASK));
    return DF_OK;
}

static int print_maintenance(const struct df_gvs_runtime_sync *sync,
                             uint64_t now_ms, const char *event) {
    struct df_gvs_runtime_sync_status status;
    const char *phase;
    const char *role;

    if (status_snapshot(sync, &status, &phase, &role) != DF_OK) {
        return DF_ERR_INVALID;
    }
    printf("{\"time_ms\":%llu,\"event\":\"%s\",\"role\":\"%s\","
           "\"phase\":\"%s\",\"version\":%u,\"online_peers\":%zu,"
           "\"peer_presence_evidence_gap\":true,"
           "\"periodic_misses\":%u}\n",
           (unsigned long long)now_ms, event, role, phase,
           (unsigned)status.sync_version, status.online_peers,
           status.periodic_misses);
    return DF_OK;
}

static int print_call(struct df_gvs_peer_sim *sim,
                      const struct df_gvs_runtime_sync *sync,
                      const uint8_t local[6], const uint8_t destination[6],
                      const char *scope, uint64_t now_ms,
                      bool expected_accepted) {
    const uint8_t *frame;
    size_t length;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_receive_result result;
    struct df_gvs_runtime_sync_status status;
    const char *phase;
    const char *role;
    const char *state;

    if (df_gvs_peer_sim_make_call(sim, destination) != DF_OK ||
        df_gvs_peer_sim_next_frame(sim, &frame, &length) != DF_OK ||
        df_gvs_receive_datagram(frame, length, local, &session, &deadline,
                                now_ms, &result) != DF_OK ||
        result.accepted_call != expected_accepted ||
        status_snapshot(sync, &status, &phase, &role) != DF_OK ||
        (state = session_name(session.state)) == NULL) {
        return DF_ERR_IO;
    }
    printf("{\"time_ms\":%llu,\"event\":\"call_routing\","
           "\"phase\":\"%s\",\"role\":\"%s\",\"version\":%u,"
           "\"online_peers\":%zu,\"destination_scope\":\"%s\","
           "\"accepted_call\":%s,\"session_state\":\"%s\"}\n",
           (unsigned long long)now_ms, phase, role,
           (unsigned)status.sync_version, status.online_peers, scope,
           result.accepted_call ? "true" : "false", state);
    return DF_OK;
}

static int run_calls(struct df_gvs_peer_sim *sim,
                     const struct df_gvs_runtime_sync *sync,
                     const uint8_t local[6], uint64_t now_ms) {
    const uint8_t same_apartment[6] = {0x61, 2, 1, 1, 1, 4};
    const uint8_t other_apartment[6] = {0x61, 2, 1, 1, 2, 2};

    if (print_call(sim, sync, local, local, "local", now_ms, true) != DF_OK ||
        print_call(sim, sync, local, same_apartment, "same_apartment",
                   now_ms, true) != DF_OK ||
        print_call(sim, sync, local, other_apartment, "other_apartment",
                   now_ms, false) != DF_OK) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int run_scenario(enum df_gvs_peer_sim_scenario scenario) {
    static const uint64_t election_times[] = {
        3000, 3500, 4000, 4500, 5500, 6500, 7500,
    };
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    struct df_gvs_runtime_sync sync;
    size_t index;
    uint64_t final_time;
    int status = DF_ERR_IO;

    if (df_gvs_peer_sim_create(&sim, scenario, local, 0) != DF_OK ||
        df_gvs_runtime_sync_start(&sync, local, 7, 0) != DF_OK) {
        df_gvs_peer_sim_destroy(sim);
        return DF_ERR_IO;
    }
    if (scenario == DF_GVS_SIM_MAINTAINER_LOSS) {
        if (sim_step(sim, &sync, 3000) != DF_OK ||
            sim_step(sim, &sync, 63000) != DF_OK ||
            sim_step(sim, &sync, 123000) != DF_OK ||
            print_maintenance(&sync, 123000, "first_period_missed") != DF_OK ||
            sim_step(sim, &sync, 183000) != DF_OK ||
            print_maintenance(&sync, 183000, "maintainer_takeover") != DF_OK) {
            goto done;
        }
        final_time = 183000;
    } else {
        for (index = 0;
             index < sizeof(election_times) / sizeof(election_times[0]);
             ++index) {
            if (sim_step(sim, &sync, election_times[index]) != DF_OK) {
                goto done;
            }
        }
        final_time = 7500;
        if (print_election(&sync, sim, final_time,
                           scenario == DF_GVS_SIM_LOWER_PEER) != DF_OK) {
            goto done;
        }
    }
    if (run_calls(sim, &sync, local, final_time) != DF_OK) {
        goto done;
    }
    status = DF_OK;

done:
    df_gvs_peer_sim_destroy(sim);
    return status;
}

int main(int argc, char **argv) {
    enum df_gvs_peer_sim_scenario scenario;

    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        fprintf(stderr, "usage: %s --scenario SCENARIO\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "no-peer") == 0) {
        scenario = DF_GVS_SIM_NO_PEER;
    } else if (strcmp(argv[2], "lower-peer") == 0) {
        scenario = DF_GVS_SIM_LOWER_PEER;
    } else if (strcmp(argv[2], "maintainer-loss") == 0) {
        scenario = DF_GVS_SIM_MAINTAINER_LOSS;
    } else {
        fprintf(stderr, "unknown scenario: %s\n", argv[2]);
        return 2;
    }
    return run_scenario(scenario) == DF_OK ? 0 : 1;
}
