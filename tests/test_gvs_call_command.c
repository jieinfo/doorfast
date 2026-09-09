#include <string.h>

#include "gvs_call_command.h"
#include "gvs_frame.h"
#include "test.h"

static int call_command_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)context;
    memset(random_code, 0x31, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0x41, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

static struct df_gvs_session ringing_session(void) {
    const struct df_gvs_session session = {
        .state = DF_GVS_RINGING,
        .peer = {0x32, 2, 1, 0, 1, 0},
        .generation = 7,
    };

    return session;
}

void test_gvs_call_command_prepares_exact_answer_frame(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t expected_payload[7] = {
        0x02, 0x20, 0x6F, 0x00, 0x20, 0x6E, 0x78,
    };
    struct df_gvs_session session = ringing_session();
    struct df_gvs_session before = session;
    struct df_gvs_call_command command;
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t packet[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
    size_t length = 0;

    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_call_command_prepare_answer(
                   &session, 7, local, 8303, 8302, 120, &command));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &session, sizeof(session)));
    TEST_ASSERT_INT_EQ(1, command.valid);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, command.type);
    TEST_ASSERT_INT_EQ(0, memcmp(session.peer, command.destination, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(local, command.source, 6));
    TEST_ASSERT_INT_EQ(0, memcmp(expected_payload, command.payload, 7));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_call_command_serialize(
                   &command, packet, sizeof(packet), &length,
                   call_command_header_fields, NULL));
    TEST_ASSERT_INT_EQ(49, (int)length);
    TEST_ASSERT_INT_EQ(
        DF_OK, df_gvs_frame_parse(packet, length, &frame, &event));
    TEST_ASSERT_INT_EQ(0x03, frame.family);
    TEST_ASSERT_INT_EQ(0x03, frame.opcode);
    TEST_ASSERT_INT_EQ(7, frame.payload_length);
    TEST_ASSERT_INT_EQ(0, memcmp(expected_payload, frame.payload, 7));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
}

void test_gvs_call_command_prepares_hangup_for_each_active_state(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const enum df_gvs_session_state states[] = {
        DF_GVS_PREVIEW, DF_GVS_RINGING, DF_GVS_TALKING,
    };
    size_t index;

    for (index = 0; index < sizeof(states) / sizeof(states[0]); index++) {
        struct df_gvs_session session = ringing_session();
        struct df_gvs_call_command command;
        uint8_t packet[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
        size_t length = 0;

        session.state = states[index];
        TEST_ASSERT_INT_EQ(
            DF_OK, df_gvs_call_command_prepare_hangup(
                       &session, 7, local, 0x01, &command));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_HANGUP, command.type);
        TEST_ASSERT_INT_EQ(1, command.payload_length);
        TEST_ASSERT_INT_EQ(0x01, command.payload[0]);
        TEST_ASSERT_INT_EQ(
            DF_OK, df_gvs_call_command_serialize(
                       &command, packet, sizeof(packet), &length,
                       call_command_header_fields, NULL));
        TEST_ASSERT_INT_EQ(43, (int)length);
        TEST_ASSERT_INT_EQ(0x03, packet[38]);
        TEST_ASSERT_INT_EQ(0x02, packet[39]);
        TEST_ASSERT_INT_EQ(0x01, packet[42]);
        TEST_ASSERT_INT_EQ(states[index], session.state);
    }
}

void test_gvs_call_command_rejects_stale_or_invalid_session(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = ringing_session();
    struct df_gvs_call_command command;
    uint8_t packet[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
    size_t length = 99;

    memset(&command, 0xA5, sizeof(command));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_call_command_prepare_answer(
                            &session, 6, local, 8303, 8302, 120, &command));
    TEST_ASSERT_INT_EQ(0, command.valid);
    TEST_ASSERT_INT_EQ(0, (int)command.session_generation);
    session.state = DF_GVS_TALKING;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_call_command_prepare_answer(
                            &session, 7, local, 8303, 8302, 120, &command));
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_call_command_prepare_hangup(
                            &session, 7, local, 1, &command));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID, df_gvs_call_command_serialize(
                            &command, packet, sizeof(packet), &length,
                            call_command_header_fields, NULL));
    TEST_ASSERT_INT_EQ(0, (int)length);
    session = ringing_session();
    {
        const uint16_t ports[][2] = {{0, 8302}, {8303, 0}, {8303, 8302}};
        const uint8_t durations[] = {120, 120, 0};
        const struct df_gvs_call_command empty = {0};
        size_t index;

        for (index = 0; index < 3; index++) {
            memset(&command, 0xA5, sizeof(command));
            TEST_ASSERT_INT_EQ(
                DF_ERR_INVALID, df_gvs_call_command_prepare_answer(
                    &session, 7, local, ports[index][0], ports[index][1],
                    durations[index], &command));
            TEST_ASSERT_INT_EQ(0, memcmp(&empty, &command, sizeof(command)));
        }
        TEST_ASSERT_INT_EQ(
            DF_ERR_INVALID, df_gvs_call_command_prepare_hangup(
                NULL, 7, local, 1, &command));
        TEST_ASSERT_INT_EQ(0, command.valid);
        TEST_ASSERT_INT_EQ(
            DF_ERR_INVALID, df_gvs_call_command_prepare_answer(
                &session, 7, NULL, 8303, 8302, 120, &command));
        memset(session.peer, 0, sizeof(session.peer));
        TEST_ASSERT_INT_EQ(
            DF_ERR_INVALID, df_gvs_call_command_prepare_hangup(
                &session, 7, local, 1, &command));
    }
}
