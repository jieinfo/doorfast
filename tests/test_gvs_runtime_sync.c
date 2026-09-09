#include <string.h>

#include "gvs_runtime_sync.h"
#include "gvs_serialize.h"
#include "test.h"

static int runtime_sync_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)context;
    memset(random_code, 0x31, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x41, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

static int runtime_sync_count_action(
    const struct df_gvs_presence_action *action, void *context) {
    unsigned *count = context;

    if (action == NULL || count == NULL) {
        return DF_ERR_IO;
    }
    (*count)++;
    return DF_OK;
}

void test_gvs_runtime_sync_routes_only_sync_frames(void) {
    const uint8_t local[6] = {0x61, 1, 1, 1, 1, 2};
    const uint8_t remote[6] = {0x61, 1, 1, 1, 1, 1};
    struct df_gvs_runtime_sync sync;
    struct df_gvs_runtime_sync_result result;
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PERIODIC_SYNC,
        .target = {0x61, 1, 1, 1, 1, 2},
    };
    struct df_gvs_sync_store remote_store;
    uint8_t packet[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length;
    unsigned action_count = 0;

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_start(&sync, local, 7, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(
                                  packet, sizeof(packet), &length, local,
                                  remote, 0x03, 0x01, NULL, 0,
                                  runtime_sync_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_receive(
                                  &sync, packet, length, 1, &result));
    TEST_ASSERT_INT_EQ(0, result.handled);
    TEST_ASSERT_INT_EQ(7, sync.presence.sync_version);

    df_gvs_sync_store_init(&remote_store);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&sync.store, "mode", "home"));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&remote_store, "mode", "away"));
    sync.presence.phase = DF_GVS_PRESENCE_PERIODIC;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_normal_serialize(
                                  &remote_store, "mode", local, remote, 8,
                                  packet, sizeof(packet), &length,
                                  runtime_sync_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_receive(
                                  &sync, packet, length, 2, &result));
    TEST_ASSERT_INT_EQ(1, result.handled);
    TEST_ASSERT_INT_EQ(1, result.accepted);
    TEST_ASSERT_INT_EQ(1, result.version_changed);
    TEST_ASSERT_INT_EQ(8, sync.presence.sync_version);
    TEST_ASSERT_INT_EQ(0, strcmp("away", sync.store.entries[0].value));

    packet[40]++;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_receive(
                                  &sync, packet, length, 3, &result));
    TEST_ASSERT_INT_EQ(1, result.handled);
    TEST_ASSERT_INT_EQ(1, result.rejected);
    TEST_ASSERT_INT_EQ(8, sync.presence.sync_version);

    df_gvs_runtime_sync_stop(&sync);
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_DOWN, sync.presence.phase);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_restart(&sync, 100));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_WAIT_SYNC, sync.presence.phase);
    TEST_ASSERT_INT_EQ(8, sync.presence.sync_version);
    TEST_ASSERT_INT_EQ(0, strcmp("away", sync.store.entries[0].value));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_tick(
                                  &sync, 1100, runtime_sync_count_action,
                                  &action_count));
    TEST_ASSERT_INT_EQ(5, (int)action_count);

    action.type = DF_GVS_PRESENCE_PERIODIC_SYNC;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_periodic_serialize(
                                  &remote_store, 0, &action, remote, 9,
                                  packet, sizeof(packet), &length,
                                  runtime_sync_fields, NULL));
}

void test_gvs_runtime_sync_exposes_redacted_status_snapshot(void) {
    const uint8_t local[6] = {0x61, 1, 1, 1, 1, 2};
    const uint8_t remote[6] = {0x61, 1, 1, 1, 1, 1};
    struct df_gvs_runtime_sync sync;
    struct df_gvs_runtime_sync_result result;
    struct df_gvs_runtime_sync_status status;
    uint8_t packet[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t packet_length;
    char json[512];
    char small[8] = "dirty";

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_start(&sync, local, 23, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_WAIT_SYNC, status.phase);
    TEST_ASSERT_INT_EQ(DF_GVS_SYNC_ROLE_STARTING, status.role);
    TEST_ASSERT_INT_EQ(23, status.sync_version);
    TEST_ASSERT_INT_EQ(2, (int)status.registered_adapters);
    TEST_ASSERT_INT_EQ(0, (int)status.enabled_adapters);
    TEST_ASSERT_INT_EQ(0, (int)status.online_peers);

    sync.presence.phase = DF_GVS_PRESENCE_PERIODIC;
    sync.presence.sync_maintainer = false;
    sync.presence.periodic_misses = 1;
    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_gvs_sync_adapter_enable(&sync.adapters, &sync.store,
                                   "sync_mini1_secretkey", "synthetic"));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(
                                  packet, sizeof(packet), &packet_length,
                                  local, remote, 0x91, 0x01, NULL, 0,
                                  runtime_sync_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_receive(
                                  &sync, packet, packet_length, 1, &result));

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_runtime_sync_status_json(&sync, json,
                                                       sizeof(json)));
    TEST_ASSERT_INT_EQ(
        0,
        strcmp("{\"phase\":\"periodic\",\"role\":\"follower\","
               "\"sync_version\":23,\"periodic_misses\":1,"
               "\"online_peers\":0,\"adapters\":{\"registered\":2,"
               "\"enabled\":1},\"last_receive\":{\"opcode\":1,"
               "\"handled\":true,\"accepted\":true,\"rejected\":false,"
               "\"resend_local\":false}}",
               json));
    TEST_ASSERT_INT_EQ(1, strstr(json, "secretkey") == NULL);
    TEST_ASSERT_INT_EQ(1, strstr(json, "synthetic") == NULL);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_runtime_sync_status_json(&sync, small,
                                                       sizeof(small)));
    TEST_ASSERT_INT_EQ(0, small[0]);

    const uint8_t ping_reply[6] = {0, 1, 0, 0, 0, 0};
    unsigned observations = 0;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(
        packet, sizeof(packet), &packet_length, local, remote, 7, 0x81,
        ping_reply, sizeof(ping_reply), runtime_sync_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_peer(
        &sync.presence, packet, packet_length, 1,
        runtime_sync_count_action, &observations));
    TEST_ASSERT_INT_EQ(1, (int)observations);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status(&sync, &status));
    TEST_ASSERT_INT_EQ(1, (int)status.online_peers);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_runtime_sync_status_json(
                                  &sync, json, sizeof(json)));
    TEST_ASSERT_INT_EQ(1, strstr(json, "\"online_peers\":1") != NULL);
}

void test_gvs_runtime_sync_names_only_valid_public_states(void) {
    TEST_ASSERT_INT_EQ(
        0, strcmp("down",
                  df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_DOWN)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("wait_sync", df_gvs_runtime_sync_phase_name(
                                   DF_GVS_PRESENCE_WAIT_SYNC)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("sync_ask", df_gvs_runtime_sync_phase_name(
                                  DF_GVS_PRESENCE_SYNC_ASK)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("sync_choose", df_gvs_runtime_sync_phase_name(
                                     DF_GVS_PRESENCE_SYNC_CHOOSE)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("periodic",
                  df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_PERIODIC)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("maintainer", df_gvs_runtime_sync_role_name(
                                    DF_GVS_SYNC_ROLE_MAINTAINER)));
    TEST_ASSERT_INT_EQ(
        0, strcmp("follower", df_gvs_runtime_sync_role_name(
                                  DF_GVS_SYNC_ROLE_FOLLOWER)));
    TEST_ASSERT_INT_EQ(
        1, df_gvs_runtime_sync_phase_name(
               (enum df_gvs_presence_phase)99) == NULL);
    TEST_ASSERT_INT_EQ(
        1,
        df_gvs_runtime_sync_role_name((enum df_gvs_sync_role)99) == NULL);
}
