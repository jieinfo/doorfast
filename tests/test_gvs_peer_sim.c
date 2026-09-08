#include <string.h>

#include "gvs_frame.h"
#include "gvs_peer_sim.h"
#include "gvs_runtime_sync.h"
#include "test.h"

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
                                        &result) != DF_OK) {
            return DF_ERR_IO;
        }
    }
    if (df_gvs_runtime_sync_tick(sync, now_ms, df_gvs_peer_sim_emit, sim) !=
        DF_OK) {
        return DF_ERR_IO;
    }
    while (df_gvs_peer_sim_next_frame(sim, &frame, &length) == DF_OK) {
        if (df_gvs_runtime_sync_receive(sync, frame, length, now_ms,
                                        &result) != DF_OK) {
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

static int sim_run_election(struct df_gvs_peer_sim *sim,
                            struct df_gvs_runtime_sync *sync) {
    static const uint64_t times[] = {3000, 3500, 4000, 4500,
                                     5500, 6500, 7500};
    size_t index;

    for (index = 0; index < sizeof(times) / sizeof(times[0]); ++index) {
        if (sim_step(sim, sync, times[index]) != DF_OK) {
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

void test_gvs_peer_sim_rejects_invalid_time_and_bounds_queue(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    unsigned i;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_create(
                                  &sim, DF_GVS_SIM_NO_PEER, local, 100));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_peer_sim_advance(sim, 99));
    for (i = 0; i < DF_GVS_SIM_FRAME_CAPACITY; ++i) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_make_call(sim, local));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_peer_sim_make_call(sim, local));
    df_gvs_peer_sim_destroy(sim);
}

void test_gvs_peer_sim_builds_synthetic_call_frame(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t expected_door[6] = {0x32, 2, 1, 0, 1, 0};
    struct df_gvs_peer_sim *sim = NULL;
    const uint8_t *data = NULL;
    size_t length = 0;
    struct df_gvs_frame frame;
    struct df_event event;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_create(
                                  &sim, DF_GVS_SIM_NO_PEER, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_make_call(sim, local));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_peer_sim_next_frame(sim, &data, &length));
    TEST_ASSERT_INT_EQ(42, (int)length);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_frame_parse(data, length, &frame, &event));
    TEST_ASSERT_INT_EQ(0, memcmp(local, frame.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(expected_door, frame.source, 6));
    TEST_ASSERT_INT_EQ(0x03, frame.family);
    TEST_ASSERT_INT_EQ(0x01, frame.opcode);
    TEST_ASSERT_INT_EQ(0, frame.payload_length);
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
                       df_gvs_peer_sim_next_frame(sim, &data, &length));
    df_gvs_peer_sim_destroy(sim);
}

void test_gvs_peer_sim_elects_maintainer_without_peers(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    struct df_gvs_runtime_sync sync;
    struct df_gvs_runtime_sync_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_create(
                                  &sim, DF_GVS_SIM_NO_PEER, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_start(&sync, local, 7, 0));
    TEST_ASSERT_INT_EQ(DF_OK, sim_run_election(sim, &sync));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_PERIODIC, status.phase);
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_MAINTAINER, status.role);
    TEST_ASSERT_INT_EQ(9, (int)df_gvs_peer_sim_action_count(
                              sim, DF_GVS_PRESENCE_SYNC_ASK_ACTION));
    TEST_ASSERT_INT_EQ(9, (int)df_gvs_peer_sim_action_count(
                              sim, DF_GVS_PRESENCE_SYNC_VERSION_ASK));
    TEST_ASSERT_INT_EQ(0, (int)status.online_peers);
    df_gvs_peer_sim_destroy(sim);
}

void test_gvs_peer_sim_follows_lower_peer_sync_replies(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    struct df_gvs_runtime_sync sync;
    struct df_gvs_runtime_sync_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_create(
                                  &sim, DF_GVS_SIM_LOWER_PEER, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_start(&sync, local, 7, 0));
    TEST_ASSERT_INT_EQ(DF_OK, sim_run_election(sim, &sync));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_PERIODIC, status.phase);
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_FOLLOWER, status.role);
    TEST_ASSERT_INT_EQ(7, status.sync_version);
    TEST_ASSERT_INT_EQ(0, (int)status.online_peers);
    df_gvs_peer_sim_destroy(sim);
}

void test_gvs_peer_sim_takes_over_after_two_missed_periods(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    struct df_gvs_runtime_sync sync;
    struct df_gvs_runtime_sync_status status;

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_peer_sim_create(
                           &sim, DF_GVS_SIM_MAINTAINER_LOSS, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_start(&sync, local, 7, 0));
    TEST_ASSERT_INT_EQ(DF_OK, sim_step(sim, &sync, 3000));
    TEST_ASSERT_INT_EQ(DF_OK, sim_step(sim, &sync, 63000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_FOLLOWER, status.role);
    TEST_ASSERT_INT_EQ(0, (int)status.periodic_misses);
    TEST_ASSERT_INT_EQ(DF_OK, sim_step(sim, &sync, 123000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_FOLLOWER, status.role);
    TEST_ASSERT_INT_EQ(1, (int)status.periodic_misses);
    TEST_ASSERT_INT_EQ(DF_OK, sim_step(sim, &sync, 183000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_MAINTAINER, status.role);
    TEST_ASSERT_INT_EQ(0, (int)status.periodic_misses);
    TEST_ASSERT_INT_EQ(3, (int)df_gvs_peer_sim_action_count(
                              sim, DF_GVS_PRESENCE_PERIODIC_SYNC));
    TEST_ASSERT_INT_EQ(0, (int)status.online_peers);
    df_gvs_peer_sim_destroy(sim);
}
