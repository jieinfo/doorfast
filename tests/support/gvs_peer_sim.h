#ifndef DOORFAST_TEST_GVS_PEER_SIM_H
#define DOORFAST_TEST_GVS_PEER_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "gvs_presence.h"

#define DF_GVS_SIM_FRAME_CAPACITY 16U

enum df_gvs_peer_sim_scenario {
    DF_GVS_SIM_NO_PEER = 0,
    DF_GVS_SIM_LOWER_PEER,
    DF_GVS_SIM_MAINTAINER_LOSS,
};

struct df_gvs_peer_sim;

int df_gvs_peer_sim_create(struct df_gvs_peer_sim **sim,
                           enum df_gvs_peer_sim_scenario scenario,
                           const uint8_t local[6], uint64_t now_ms);
void df_gvs_peer_sim_destroy(struct df_gvs_peer_sim *sim);
int df_gvs_peer_sim_emit(const struct df_gvs_presence_action *action,
                         void *context);
int df_gvs_peer_sim_advance(struct df_gvs_peer_sim *sim, uint64_t now_ms);
int df_gvs_peer_sim_next_frame(struct df_gvs_peer_sim *sim,
                               const uint8_t **frame, size_t *length);
int df_gvs_peer_sim_make_call(struct df_gvs_peer_sim *sim,
                              const uint8_t destination[6]);
int df_gvs_peer_sim_make_peer_online(struct df_gvs_peer_sim *sim);
int df_gvs_peer_sim_make_periodic_sync(struct df_gvs_peer_sim *sim);
int df_gvs_peer_sim_make_normal_update(struct df_gvs_peer_sim *sim);
size_t df_gvs_peer_sim_action_count(
    const struct df_gvs_peer_sim *sim,
    enum df_gvs_presence_action_type type);

#endif
