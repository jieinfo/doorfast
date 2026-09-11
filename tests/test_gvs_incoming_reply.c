#include <string.h>

#include "gvs_frame.h"
#include "gvs_incoming_reply.h"
#include "gvs_memory_sender.h"
#include "gvs_receive.h"
#include "test.h"

static size_t make_observed_call(uint8_t packet[64],
                                 const uint8_t destination[6],
                                 const uint8_t source[6]) {
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5,
    };

    static const uint8_t sanitized_payload[9] = {
        0x01, 0x20, 0x6f, 0x00, 0x20, 0x6e, 0x1e, 0x00, 0x00,
    };

    memset(packet, 0, 64);
    memcpy(packet, magic, sizeof(magic));
    memcpy(packet + 10, destination, 6);
    memcpy(packet + 16, source, 6);
    packet[38] = 0x03;
    packet[39] = 0x01;
    memcpy(packet + 42, sanitized_payload, sizeof(sanitized_payload));
    /* Field samples declare 15 while carrying nine bytes for 03/01. */
    packet[40] = 0x0f;
    packet[41] = 0x00;
    return 42U + sizeof(sanitized_payload);
}

static size_t make_hangup(uint8_t packet[64], const uint8_t destination[6],
                          const uint8_t source[6]) {
    (void)make_observed_call(packet, destination, source);

    packet[39] = 0x02;
    packet[40] = 0x01;
    packet[42] = 0x01;
    return 43U;
}

void test_gvs_incoming_reply_matches_observed_0381(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    const uint8_t expected_payload[7] = {
        0x01, 0x00, 0x02, 0x20, 0x6f, 0x1e, 0x01,
    };
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 9,
    };
    struct df_gvs_receive_result observed = {.accepted_call = true};
    struct df_gvs_incoming_reply reply;
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t bytes[DF_GVS_INCOMING_REPLY_FRAME_SIZE];
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_incoming_reply_prepare(
        &observed, &session, 9, local, 8303, &reply));
    TEST_ASSERT_INT_EQ(1, reply.valid);
    TEST_ASSERT_INT_EQ(0, memcmp(reply.destination, peer, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(reply.source, local, 6));
    TEST_ASSERT_INT_EQ(9, (int)reply.session_generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_incoming_reply_serialize(
        &reply, bytes, sizeof(bytes), &length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(49, (int)length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(bytes, length, &frame, &event));
    TEST_ASSERT_INT_EQ(0x03, frame.family);
    TEST_ASSERT_INT_EQ(0x81, frame.opcode);
    TEST_ASSERT_INT_EQ(7, frame.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(expected_payload, frame.payload, 7));
}

void test_gvs_incoming_reply_tracks_each_retransmission_without_new_session(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_receive_result observed;
    struct df_gvs_incoming_reply reply;
    uint8_t packet[64];
    uint8_t reply_bytes[DF_GVS_INCOMING_REPLY_FRAME_SIZE];
    size_t length = make_observed_call(packet, local, peer);
    size_t reply_length;
    unsigned index;

    for (index = 0; index < 3U; ++index) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(
            packet, length, local, &session, &deadline,
            100U + index * 100U, &observed));
        TEST_ASSERT_INT_EQ(1, observed.accepted_call);
        TEST_ASSERT_INT_EQ(index == 0U ? 1 : 0,
                           (int)observed.transition.count);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_incoming_reply_prepare(
            &observed, &session, 1, local, 8303, &reply));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_incoming_reply_serialize(
            &reply, reply_bytes, sizeof(reply_bytes), &reply_length,
            df_gvs_placeholder_header_fields, NULL));
        TEST_ASSERT_INT_EQ(49, (int)reply_length);
        TEST_ASSERT_INT_EQ(0x81, reply_bytes[39]);
        TEST_ASSERT_INT_EQ(1, reply.valid);
        TEST_ASSERT_INT_EQ(1, (int)reply.session_generation);
        TEST_ASSERT_INT_EQ(100, (int)deadline.started_ms);
        TEST_ASSERT_INT_EQ(30000, (int)deadline.timeout_ms);
    }
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(1, (int)session.generation);

    length = make_hangup(packet, local, peer);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_receive_datagram(
        packet, length, local, &session, &deadline, 500, &observed));
    TEST_ASSERT_INT_EQ(1, observed.ended_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_incoming_reply_prepare(
        &observed, &session, 1, local, 8303, &reply));
}

void test_gvs_incoming_reply_rejects_stale_or_non_call_observation(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 9,
    };
    struct df_gvs_receive_result observed = {0};
    struct df_gvs_incoming_reply reply;
    const struct df_gvs_incoming_reply empty = {0};

    memset(&reply, 0xa5, sizeof(reply));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_incoming_reply_prepare(
        &observed, &session, 9, local, 8303, &reply));
    TEST_ASSERT_INT_EQ(0, memcmp(&empty, &reply, sizeof(reply)));
    observed.accepted_call = true;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_incoming_reply_prepare(
        &observed, &session, 8, local, 8303, &reply));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_incoming_reply_prepare(
        &observed, &session, 9, local, 0, &reply));
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_incoming_reply_prepare(
        &observed, &session, 9, local, 8303, &reply));
}
