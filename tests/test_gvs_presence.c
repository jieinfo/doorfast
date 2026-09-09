#include <string.h>

#include "gvs_presence.h"
#include "test.h"

#define ACTION_CAPACITY 128

void test_gvs_presence_receives_sync_replies(void) {
    const uint8_t identity[6] = {0x61, 0x01, 0x01, 0x01, 0x01, 0x02};
    uint8_t packet[45] = {'G', 'V', 'S', 'G', 'V', 'S',
                           0xa5, 0xa5, 0xa5, 0xa5};
    struct df_gvs_presence presence, before;
    unsigned version, extension;
    memcpy(packet + 10, identity, 6);
    memcpy(packet + 16, identity, 6);
    packet[21] = 1;
    packet[38] = 0x91;
    packet[39] = 0x82;
    packet[40] = 2;
    /* Versions around 0x0100 prove little-endian comparison. */
    for (version = 255; version <= 257; version++) {
        for (extension = 1; extension <= 3; extension += 2) {
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 0));
            presence.phase = DF_GVS_PRESENCE_SYNC_CHOOSE;
            presence.sync_maintainer = true;
            presence.sync_version = 256;
            packet[21] = (uint8_t)extension;
            packet[42] = (uint8_t)version;
            packet[43] = (uint8_t)(version >> 8);
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_sync(&presence, packet, 44, 10));
            TEST_ASSERT_INT_EQ(!(version >= 256 && extension < 2), presence.sync_maintainer);
            TEST_ASSERT_INT_EQ(256, presence.sync_version);
        }
    }
    packet[21] = 1;
    packet[39] = 0x81;
    presence.phase = DF_GVS_PRESENCE_SYNC_ASK;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_sync(&presence, packet, 44, 20));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_PERIODIC, presence.phase);
    TEST_ASSERT_INT_EQ(0, presence.sync_version);
    TEST_ASSERT_INT_EQ(0, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(60020, presence.next_phase_ms);
    before = presence;
    /* Duplicate must not re-arm the periodic deadline. */
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_sync(&presence, packet, 44, 20));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &presence, sizeof(presence)));
    for (unsigned kind = 0; kind < 7; kind++) {
        uint8_t bad[45];
        size_t length = 44;
        memcpy(bad, packet, sizeof(bad));
        if (kind == 0) length = 43;
        if (kind == 1) { length = 45; bad[40] = 3; }
        if (kind == 2) bad[10] ^= 1;
        if (kind == 3) bad[17] ^= 1;
        if (kind == 4) bad[21] = identity[5];
        if (kind == 5) bad[38] = 3;
        if (kind == 6) bad[39] = 0x83;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_presence_receive_sync(&presence, bad, length, 20));
        TEST_ASSERT_INT_EQ(0, memcmp(&before, &presence, sizeof(presence)));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_presence_receive_sync(&presence, packet, 44, 19));
}

struct action_log {
    struct df_gvs_presence_action actions[ACTION_CAPACITY];
    size_t count;
};

static int collect_action(const struct df_gvs_presence_action *action,
                          void *context) {
    struct action_log *log = context;

    if (action == NULL || log == NULL || log->count >= ACTION_CAPACITY) {
        return DF_ERR_IO;
    }
    log->actions[log->count++] = *action;
    return DF_OK;
}

static int reject_action(const struct df_gvs_presence_action *action,
                         void *context) {
    (void)action;
    (void)context;
    return DF_ERR_IO;
}

static size_t action_count(const struct action_log *log,
                           enum df_gvs_presence_action_type type) {
    size_t count = 0;
    size_t index;

    for (index = 0; index < log->count; index++) {
        if (log->actions[index].type == type) {
            count++;
        }
    }
    return count;
}

void test_gvs_presence_runs_probes_and_sync_phases_without_network_io(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_presence presence = {0};
    struct action_log log = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 1000, collect_action, &log));
    TEST_ASSERT_INT_EQ(5, (int)action_count(&log, DF_GVS_PRESENCE_PEER_PROBE));

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 7500, collect_action, &log));
    TEST_ASSERT_INT_EQ(9, (int)action_count(&log,
                                            DF_GVS_PRESENCE_SYNC_ASK_ACTION));
    TEST_ASSERT_INT_EQ(9, (int)action_count(&log,
                                            DF_GVS_PRESENCE_SYNC_VERSION_ASK));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_PERIODIC, presence.phase);

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 67500, collect_action, &log));
    TEST_ASSERT_INT_EQ(3, (int)action_count(&log,
                                            DF_GVS_PRESENCE_PERIODIC_SYNC));
}

void test_gvs_presence_receives_peer_online_replies(void) {
    /* Literal 07/81 reply, independent of the packet serializer. */
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    uint8_t reply[49] = {'G','V','S','G','V','S',0xa5,0xa5,0xa5,0xa5};
    struct df_gvs_presence received, before;
    struct action_log replies = {0};
    memcpy(reply + 10, local, 6);
    memcpy(reply + 16, local, 6);
    reply[21] = 1;
    reply[38] = 7;
    reply[39] = 0x81;
    reply[40] = 6;
    reply[43] = 1;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&received, local, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_peer(
        &received, reply, 48, 0, collect_action, &replies));
    TEST_ASSERT_INT_EQ(1, (int)action_count(&replies, DF_GVS_PRESENCE_PEER_ONLINE));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_tick(&received, 59000, collect_action, &replies));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_receive_peer(
        &received, reply, 48, 59000, collect_action, &replies));
    TEST_ASSERT_INT_EQ(2, (int)action_count(&replies, DF_GVS_PRESENCE_PEER_ONLINE));
    before = received;
    for (unsigned kind = 0; kind < 7; kind++) {
        uint8_t bad[49];
        size_t length = 48;
        memcpy(bad, reply, sizeof(bad));
        if (kind == 0) length = 47;
        if (kind == 1) { length = 49; bad[40] = 7; }
        if (kind == 2) bad[10] ^= 1;
        if (kind == 3) bad[17] ^= 1;
        if (kind == 4) bad[21] = 2;
        if (kind == 5) bad[38] = 0x91;
        if (kind == 6) bad[39] = 0x82;
        TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_presence_receive_peer(
            &received, bad, length, 59000, collect_action, &replies));
        TEST_ASSERT_INT_EQ(0, memcmp(&before, &received, sizeof(before)));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_presence_receive_peer(
        &received, reply, 48, 59000, reject_action, NULL));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &received, sizeof(before)));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_tick(
                                  &received, 118000, collect_action, &replies));
    size_t online = 0;
    for (size_t i = 0; i < DF_GVS_INDOOR_PEER_COUNT; i++) {
        online += received.peers[i].online;
    }
    TEST_ASSERT_INT_EQ(1, (int)online);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_tick(
                                  &received, 119000, collect_action, &replies));
    for (size_t i = 0; i < DF_GVS_INDOOR_PEER_COUNT; i++) {
        TEST_ASSERT_INT_EQ(0, received.peers[i].online);
    }
}

void test_gvs_presence_tracks_online_timeout_and_maintainer_role(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_presence presence = {0};
    struct action_log log = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 0));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 1000, collect_action, &log));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_presence_observe_peer(&presence,
                                            presence.peers[0].address,
                                            collect_action, &log));
    TEST_ASSERT_INT_EQ(1, presence.peers[0].online);
    TEST_ASSERT_INT_EQ(1, (int)action_count(&log, DF_GVS_PRESENCE_PEER_ONLINE));

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 61000, collect_action, &log));
    TEST_ASSERT_INT_EQ(0, presence.peers[0].online);
    TEST_ASSERT_INT_EQ(5, (int)action_count(&log, DF_GVS_PRESENCE_PEER_OFFLINE));

    df_gvs_presence_set_sync_maintainer(&presence, false);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 67500, collect_action, &log));
    TEST_ASSERT_INT_EQ(0, (int)action_count(&log,
                                            DF_GVS_PRESENCE_PERIODIC_SYNC));
}

void test_gvs_presence_restarts_cleanly_after_network_recovery(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_presence presence = {0};
    struct action_log before = {0};
    struct action_log after = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 100));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 3600, collect_action, &before));
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_SYNC_ASK, presence.phase);
    df_gvs_presence_stop(&presence);
    TEST_ASSERT_INT_EQ(DF_GVS_PRESENCE_DOWN, presence.phase);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_presence_tick(&presence, 5000, collect_action, &before));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 10000));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 13000, collect_action, &after));
    TEST_ASSERT_INT_EQ(5, (int)action_count(&after, DF_GVS_PRESENCE_PEER_PROBE));
    TEST_ASSERT_INT_EQ(3, (int)action_count(&after,
                                            DF_GVS_PRESENCE_SYNC_ASK_ACTION));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_presence_tick(&presence, 12999, collect_action, &after));
}

void test_gvs_presence_does_not_advance_when_action_delivery_fails(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_presence presence = {0};
    struct action_log log = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_IO,
                       df_gvs_presence_tick(&presence, 1000, reject_action, NULL));
    TEST_ASSERT_INT_EQ(61, (int)presence.peers[0].seconds_remaining);
    TEST_ASSERT_INT_EQ(1000, (int)presence.next_peer_tick_ms);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_presence_tick(&presence, 1000, collect_action, &log));
    TEST_ASSERT_INT_EQ(5, (int)action_count(&log, DF_GVS_PRESENCE_PEER_PROBE));

    TEST_ASSERT_INT_EQ(
        DF_ERR_IO, df_gvs_presence_observe_peer(&presence,
                                                presence.peers[0].address,
                                                reject_action, NULL));
    TEST_ASSERT_INT_EQ(0, presence.peers[0].online);
}

void test_gvs_presence_takes_over_after_two_missed_periods(void) {
    uint8_t identity[6] = {0};
    struct df_gvs_presence presence = {0};
    struct action_log log = {0};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-2", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, identity, 0));
    presence.phase = DF_GVS_PRESENCE_PERIODIC;
    presence.sync_maintainer = false;
    presence.next_phase_ms = 60000;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_tick(
                                  &presence, 60000, collect_action, &log));
    TEST_ASSERT_INT_EQ(0, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(1, (int)presence.periodic_misses);
    TEST_ASSERT_INT_EQ(0, (int)action_count(
                                  &log, DF_GVS_PRESENCE_PERIODIC_SYNC));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_tick(
                                  &presence, 120000, collect_action, &log));
    TEST_ASSERT_INT_EQ(1, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(0, (int)presence.periodic_misses);
    TEST_ASSERT_INT_EQ(3, (int)action_count(
                                  &log, DF_GVS_PRESENCE_PERIODIC_SYNC));
}
