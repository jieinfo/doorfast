#include <string.h>

#include "gvs_frame.h"
#include "gvs_peer_sim.h"
#include "test.h"

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
