#include <stdio.h>
#include <string.h>

#include "gvs_frame.h"
#include "gvs_sync.h"
#include "test.h"

static int sync_header_fields(const struct df_gvs_header_request *request,
                              uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
                              uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE],
                              void *context) {
    (void)request;
    (void)context;
    memset(random_code, 0x11, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x22, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

static int bytes_equal(const uint8_t *data, size_t length,
                       const char *expected) {
    size_t expected_length = strlen(expected);
    return length == expected_length &&
           memcmp(data, expected, expected_length) == 0;
}

void test_gvs_sync_updates_version_and_registered_values(void) {
    struct df_gvs_sync_store store;
    uint16_t version = 0;

    df_gvs_sync_store_init(&store);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&store, "mode", "home"));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&store, "mode", "ignored"));
    TEST_ASSERT_INT_EQ(1, (int)store.count);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_store_update_local(
                                  &store, &version, "mode", "away"));
    TEST_ASSERT_INT_EQ(1, version);
    TEST_ASSERT_INT_EQ(0, strcmp("away", store.entries[0].value));
    version = 59999;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_store_update_local(
                                  &store, &version, "mode", "night"));
    TEST_ASSERT_INT_EQ(60000, version);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_store_update_local(
                                  &store, &version, "mode", "home"));
    TEST_ASSERT_INT_EQ(1, version);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_sync_store_update_local(
                                           &store, &version, "unknown", "x"));
    TEST_ASSERT_INT_EQ(1, version);
}

void test_gvs_sync_serializes_periodic_chunks_and_json_escaping(void) {
    const uint8_t local[6] = {0x61, 1, 1, 1, 1, 2};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PERIODIC_SYNC,
        .target = {0x61, 1, 1, 1, 1, 1},
    };
    struct df_gvs_sync_store store;
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t packet[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length = 0;
    char key[24];
    size_t index;

    df_gvs_sync_store_init(&store);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&store, "mode", "home"));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_store_register(
                                  &store, "quote", "a\"b\\c"));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_periodic_serialize(
                   &store, 0, &action, local, 0x1234, packet, sizeof(packet),
                   &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_frame_parse(packet, length, &frame, &event));
    TEST_ASSERT_INT_EQ(0x91, frame.family);
    TEST_ASSERT_INT_EQ(0x03, frame.opcode);
    TEST_ASSERT_INT_EQ(0x34, frame.payload[0]);
    TEST_ASSERT_INT_EQ(0x12, frame.payload[1]);
    TEST_ASSERT_INT_EQ(
        1, bytes_equal(frame.payload + 2, frame.payload_length - 2,
                       "{\"TYPE\":\"Period\",\"COUNT\":2,\"INFO\":["
                       "{\"KEY\":\"mode\",\"VALUE\":\"home\"},"
                       "{\"KEY\":\"quote\",\"VALUE\":\"a\\\"b\\\\c\"}]}"));

    for (index = 2; index < 21; index++) {
        (void)snprintf(key, sizeof(key), "key%02zu", index);
        TEST_ASSERT_INT_EQ(DF_OK,
                           df_gvs_sync_store_register(&store, key, "v"));
    }
    TEST_ASSERT_INT_EQ(2, (int)df_gvs_sync_periodic_chunk_count(&store));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_periodic_serialize(
                   &store, 1, &action, local, 7, packet, sizeof(packet),
                   &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_frame_parse(packet, length, &frame, &event));
    TEST_ASSERT_INT_EQ(
        1, bytes_equal(frame.payload + 2, frame.payload_length - 2,
                       "{\"TYPE\":\"Period\",\"COUNT\":1,\"INFO\":["
                       "{\"KEY\":\"key20\",\"VALUE\":\"v\"}]}"));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_sync_periodic_serialize(
                            &store, 2, &action, local, 7, packet,
                            sizeof(packet), &length, sync_header_fields, NULL));
}

void test_gvs_sync_serializes_normal_update_and_rejects_oversize(void) {
    const uint8_t local[6] = {0x61, 1, 1, 1, 1, 2};
    const uint8_t target[6] = {0x61, 1, 1, 1, 1, 1};
    struct df_gvs_sync_store store;
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t packet[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length = 99;
    char too_long[DF_GVS_SYNC_VALUE_SIZE + 1U];
    uint16_t version = 9;

    df_gvs_sync_store_init(&store);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&store, "mode", "away"));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_normal_serialize(
                   &store, "mode", target, local, 9, packet, sizeof(packet),
                   &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_frame_parse(packet, length, &frame, &event));
    TEST_ASSERT_INT_EQ(
        1, bytes_equal(frame.payload + 2, frame.payload_length - 2,
                       "{\"TYPE\":\"Normal\",\"COUNT\":1,\"INFO\":["
                       "{\"KEY\":\"mode\",\"VALUE\":\"away\"}]}"));
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_sync_store_update_local(
                           &store, &version, "mode", too_long));
    TEST_ASSERT_INT_EQ(9, version);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_sync_normal_serialize(
                            &store, "missing", target, local, 9, packet,
                            sizeof(packet), &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
}

void test_gvs_sync_receives_periodic_data_and_arbitrates_maintainer(void) {
    const uint8_t local[6] = {0x61, 1, 1, 1, 1, 2};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_PERIODIC_SYNC,
        .target = {0x61, 1, 1, 1, 1, 2},
    };
    struct df_gvs_sync_store local_store;
    struct df_gvs_sync_store remote_store;
    struct df_gvs_presence presence;
    struct df_gvs_presence before_presence;
    struct df_gvs_sync_store before_store;
    uint8_t packet[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length;
    bool resend;

    df_gvs_sync_store_init(&local_store);
    df_gvs_sync_store_init(&remote_store);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&local_store, "mode", "home"));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_sync_store_register(&remote_store, "mode", "away"));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_presence_start(&presence, local, 0));
    presence.phase = DF_GVS_PRESENCE_PERIODIC;
    presence.sync_version = 5;
    presence.sync_maintainer = false;
    presence.periodic_misses = 1;
    presence.next_phase_ms = 60000;
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_periodic_serialize(
                   &remote_store, 0, &action,
                   (const uint8_t[]){0x61, 1, 1, 1, 1, 1}, 6, packet,
                   sizeof(packet), &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_receive(
                                  &local_store, &presence, packet, length,
                                  50000, &resend));
    TEST_ASSERT_INT_EQ(0, resend);
    TEST_ASSERT_INT_EQ(0, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(5, presence.sync_version);
    TEST_ASSERT_INT_EQ(0, (int)presence.periodic_misses);
    TEST_ASSERT_INT_EQ(110000, (int)presence.next_phase_ms);
    TEST_ASSERT_INT_EQ(0, strcmp("away", local_store.entries[0].value));

    presence.sync_version = 9;
    presence.sync_maintainer = false;
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_periodic_serialize(
                   &remote_store, 0, &action,
                   (const uint8_t[]){0x61, 1, 1, 1, 1, 1}, 8, packet,
                   sizeof(packet), &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_receive(
                                  &local_store, &presence, packet, length,
                                  51000, &resend));
    TEST_ASSERT_INT_EQ(1, resend);
    TEST_ASSERT_INT_EQ(1, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(9, presence.sync_version);

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_periodic_serialize(
                   &remote_store, 0, &action,
                   (const uint8_t[]){0x61, 1, 1, 1, 1, 1}, 9, packet,
                   sizeof(packet), &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_receive(
                                  &local_store, &presence, packet, length,
                                  52000, &resend));
    TEST_ASSERT_INT_EQ(0, resend);
    TEST_ASSERT_INT_EQ(0, presence.sync_maintainer);

    presence.sync_maintainer = true;
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_sync_normal_serialize(
                   &remote_store, "mode", action.target,
                   (const uint8_t[]){0x61, 1, 1, 1, 1, 1}, 12, packet,
                   sizeof(packet), &length, sync_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_receive(
                                  &local_store, &presence, packet, length,
                                  53000, &resend));
    TEST_ASSERT_INT_EQ(0, resend);
    TEST_ASSERT_INT_EQ(0, presence.sync_maintainer);
    TEST_ASSERT_INT_EQ(12, presence.sync_version);

    before_presence = presence;
    before_store = local_store;
    packet[40]++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_sync_receive(
                                           &local_store, &presence, packet,
                                           length, 54000, &resend));
    TEST_ASSERT_INT_EQ(0, resend);
    TEST_ASSERT_INT_EQ(0, memcmp(&before_presence, &presence,
                                 sizeof(presence)));
    TEST_ASSERT_INT_EQ(0,
                       memcmp(&before_store, &local_store,
                              sizeof(local_store)));
}
