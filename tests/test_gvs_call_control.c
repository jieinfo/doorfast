#include <string.h>

#include "gvs_call_control.h"
#include "gvs_memory_sender.h"
#include "test.h"

/* Mirrors the capture-error boundary: the runtime aborts and cancels before
 * its normal control step. This must retain the actual terminal identity. */
void test_gvs_call_control_observes_capture_error_boundary(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    for (int state = DF_GVS_PREVIEW; state <= DF_GVS_TALKING; state++) {
        struct df_gvs_session session = {.state = (enum df_gvs_session_state)state,
            .generation = 17, .peer = {0x32, 2, 1, 0, 1, 0}};
        struct df_gvs_session previous = session;
        struct df_gvs_deadline deadline = {0};
        struct df_gvs_call_control control;
        struct df_gvs_call_control_status status;
        struct df_gvs_call_control_result result;
        bool ended = false;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0,
            df_gvs_placeholder_header_fields, NULL));
        df_gvs_call_control_observe_external_end(&control, &previous, &session);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_NONE, control.terminal.end_reason);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 0, 10000));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_abort(&session, &ended));
        df_gvs_deadline_cancel(&deadline);
        df_gvs_call_control_observe_external_end(&control, &previous, &session);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(&control, &session,
            local, &deadline, 1, &result));
        TEST_ASSERT_INT_EQ(1, ended);
        TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
        TEST_ASSERT_INT_EQ(0, deadline.armed);
        TEST_ASSERT_INT_EQ(0, result.frame_ready);
        TEST_ASSERT_INT_EQ(0, result.handshake_frame_ready);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(&control, &session, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_UNKNOWN, status.end_reason);
        TEST_ASSERT_INT_EQ(17, status.last_generation);
        TEST_ASSERT_INT_EQ(0, memcmp(status.last_station, previous.peer, 6));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
        TEST_ASSERT_INT_EQ(0, status.remaining_ms);
        {
            const struct df_gvs_call_control saved_control = control;
            const struct df_gvs_session saved_session = session;
            const struct df_gvs_deadline saved_deadline = deadline;
            df_gvs_call_control_observe_external_end(&control, &previous, &session);
            previous.generation++;
            df_gvs_call_control_observe_external_end(&control, &previous, &session);
            TEST_ASSERT_INT_EQ(0, memcmp(&saved_control, &control, sizeof(control)));
            TEST_ASSERT_INT_EQ(0, memcmp(&saved_session, &session, sizeof(session)));
            TEST_ASSERT_INT_EQ(0, memcmp(&saved_deadline, &deadline, sizeof(deadline)));
        }
    }
}

void test_gvs_call_control_handshake_memory_lifecycle(void) {
    const uint8_t local[6] = {0x61,2,1,1,1,1};
    struct df_gvs_session session = {.state=DF_GVS_RINGING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    const struct df_gvs_session original_session = session;
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control,0,df_gvs_placeholder_header_fields,NULL));
    for (unsigned i=0; i<5; ++i) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
            &control,&session,local,&deadline,i*2000,&result));
        TEST_ASSERT_INT_EQ(1,result.handshake_frame_ready);
        TEST_ASSERT_INT_EQ(42,control.handshake_sender.length);
        TEST_ASSERT_INT_EQ(0x51,control.handshake_sender.bytes[39]);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT,control.handshake_dispatch.state);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control,&session,local,&deadline,10000,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake.disconnected);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED,session.state);
    TEST_ASSERT_INT_EQ(0,deadline.armed);
    df_gvs_call_control_observe_external_end(&control, &original_session, &session);
    {
        struct df_gvs_call_control_status status;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
            &control, &session, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_HEARTBEAT_TIMEOUT,
            status.end_reason);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_HEARTBEAT_TIMEOUT,
            status.fault);
        TEST_ASSERT_INT_EQ(9, (int)status.last_generation);
        TEST_ASSERT_INT_EQ(0, memcmp(status.last_station, session.peer, 6));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
        TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
    }
}

static size_t control_reply(uint8_t out[64], const uint8_t destination[6],
    const uint8_t source[6], uint8_t opcode, const uint8_t *payload,
    size_t payload_length) {
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5
    };

    memset(out, 0, 64);
    memcpy(out, magic, sizeof(magic));
    memcpy(out + 10, destination, 6);
    memcpy(out + 16, source, 6);
    out[38] = 0x03;
    out[39] = opcode;
    out[40] = (uint8_t)payload_length;
    if (payload_length != 0U) {
        memcpy(out + 42, payload, payload_length);
    }
    return 42U + payload_length;
}

void test_gvs_call_control_records_zero_time_sync_timeout(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const enum df_gvs_session_state states[] = {DF_GVS_RINGING, DF_GVS_TALKING};
    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); index++) {
        struct df_gvs_session session = {.state = states[index], .generation = 44,
            .peer = {0x32, 2, 1, 0, 1, 0}};
        const struct df_gvs_session previous = session;
        struct df_gvs_deadline deadline = {0};
        struct df_gvs_call_control control;
        struct df_gvs_call_control_result result;
        struct df_gvs_call_control_status status;
        uint8_t packet[64];
        size_t length;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&control, 0,
            df_gvs_placeholder_header_fields, NULL));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 0, 10000));
        length = control_reply(packet, local, session.peer, 0x57,
            (const uint8_t[]){0}, 1);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(&control, packet,
            length, local, &session, &deadline, 1, &result));
        TEST_ASSERT_INT_EQ(1, result.runtime.receive.time_sync_update);
        TEST_ASSERT_INT_EQ(1, result.runtime.receive.timed_out_transition);
        TEST_ASSERT_INT_EQ(0, result.runtime.session_timed_out);
        for (unsigned phase = 0; phase < 4; phase++) {
            if (phase == 1) {
                TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(&control,
                    &session, local, &deadline, 2, &result));
            } else if (phase >= 2) {
                if (phase == 3) length = control_reply(packet, local,
                    previous.peer, 0x02, (const uint8_t[]){1}, 1);
                TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(&control,
                    packet, length, local, &session, &deadline, phase + 1, &result));
            }
            TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
            TEST_ASSERT_INT_EQ(0, deadline.armed);
            TEST_ASSERT_INT_EQ(44, session.generation);
            TEST_ASSERT_INT_EQ(0, memcmp(previous.peer, session.peer, 6));
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
                &control, &session, &deadline, &status));
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_SESSION_TIMEOUT, status.end_reason);
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
            TEST_ASSERT_INT_EQ(44, status.last_generation);
            TEST_ASSERT_INT_EQ(0, memcmp(previous.peer, status.last_station, 6));
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
            TEST_ASSERT_INT_EQ(0, status.remaining_ms);
        }
    }
}

static int reject_control_header(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)request;
    (void)random_code;
    (void)encryption_code;
    (void)context;
    return DF_ERR_INVALID;
}

static enum df_gvs_send_attempt_result successful_external_send(
    const struct df_gvs_call_command *command, unsigned attempt,
    uint64_t completion_id, void *context) {
    unsigned *calls = context;

    if (command == NULL || !command->valid || attempt == 0U ||
        completion_id == 0U || calls == NULL)
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    (*calls)++;
    return DF_GVS_SEND_ATTEMPT_SUCCESS;
}

void test_gvs_call_control_handshake_receive_and_retry(void) {
    const uint8_t local[6] = {0x61,2,1,1,1,1};
    struct df_gvs_session session = {.state=DF_GVS_RINGING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control, before;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_init(&control,0,
        df_gvs_placeholder_header_fields,NULL));
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_step(&control,&session,
        local,&deadline,0,&result));
    length=control_reply(packet,local,session.peer,0x52,NULL,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_receive(&control,packet,
        length,local,&session,&deadline,100,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake.accepted_reply);
    TEST_ASSERT_INT_EQ(0,control.handshake.missed_replies);
    TEST_ASSERT_INT_EQ(2000,control.handshake.next_probe_ms);
    length=control_reply(packet,local,session.peer,0x51,NULL,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_receive(&control,packet,
        length,local,&session,&deadline,200,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake.accepted_ask);
    TEST_ASSERT_INT_EQ(2200,control.handshake.next_probe_ms);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_receive(&control,packet,
        length,local,&session,&deadline,200,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake_action_dropped);
    struct df_gvs_call_control_status status;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_status(&control,&session,&status));
    TEST_ASSERT_INT_EQ(1,status.handshake_active);
    TEST_ASSERT_INT_EQ(1,status.handshake_dropped);
    TEST_ASSERT_INT_EQ(2000,status.handshake_next_ms);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_QUEUED,status.handshake_dispatch);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_step(&control,&session,
        local,&deadline,200,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake_frame_ready);
    TEST_ASSERT_INT_EQ(0x52,control.handshake_sender.bytes[39]);
    before=control;
    packet[16]^=1;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_control_receive(&control,
        packet,length,local,&session,&deadline,201,&result));
    TEST_ASSERT_INT_EQ(0,memcmp(&before,&control,sizeof(control)));
    control.handshake_sender.provide_fields=reject_control_header;
    for(unsigned i=0;i<3;++i) {
        TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_step(&control,&session,
            local,&deadline,2200+i*100,&result));
    }
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAILED,control.handshake_dispatch.state);
    TEST_ASSERT_INT_EQ(3,control.handshake_dispatch.attempts);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING,session.state);
    session.generation++;
    session.peer[4]=2;
    control.handshake_sender.provide_fields=df_gvs_placeholder_header_fields;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_control_step(&control,&session,
        local,&deadline,2500,&result));
    TEST_ASSERT_INT_EQ(1,result.handshake_frame_ready);
    TEST_ASSERT_INT_EQ(2,control.handshake_sender.bytes[14]);
    TEST_ASSERT_INT_EQ(1,control.handshake.missed_replies);
    TEST_ASSERT_INT_EQ(4500,control.handshake.next_probe_ms);
    before=control;
    length=control_reply(packet,local,session.peer,0x51,NULL,0);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_control_receive(&control,
        packet,length,local,&session,&deadline,2499,&result));
    TEST_ASSERT_INT_EQ(0,memcmp(&before,&control,sizeof(control)));
}

void test_gvs_call_control_answer_reaches_confirmation(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t answer_reply[7] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 0x1e};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 9,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 9, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    TEST_ASSERT_INT_EQ(49, control.sender.length);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING,
                       control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(0x03, control.sender.bytes[39]);

    length = control_reply(packet, local, session.peer, 0x83,
                           answer_reply, sizeof(answer_reply));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.talking_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
}

void test_gvs_call_control_promotes_preview_after_station_ack(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t reply[7] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
    struct df_gvs_session session = {.state = DF_GVS_PREVIEW, .generation = 9,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_submit_answer(
        &control, &session, 9, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_submit_call(
        &control, &session, 8, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_call(
        &control, &session, 9, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_CALL, control.dispatch.command.type);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING, control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    TEST_ASSERT_INT_EQ(49, (int)control.sender.length);
    TEST_ASSERT_INT_EQ(3, control.sender.bytes[39]);
    TEST_ASSERT_INT_EQ(2, control.sender.bytes[42]);
    length = control_reply(packet, local, session.peer, 0x83, reply, 7);
    packet[16]++;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(&control, packet,
        length, local, &session, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_rejected);
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
    packet[16]--;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(&control, packet,
        length, local, &session, &deadline, 11, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.talking_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
}

void test_gvs_call_control_tracks_answer_sent_by_external_transport(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 9,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    unsigned calls = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    df_gvs_call_control_set_sender(&control, successful_external_send, &calls);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 9, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, calls);
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING,
                       control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(0, (int)control.sender.length);
    TEST_ASSERT_INT_EQ(9, (int)session.pick_generation);
}

void test_gvs_call_control_reports_confirmation_timeout_once(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 4,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(
        &deadline, &session, 0, 2000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_hangup(
        &control, &session, 4, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(1, result.confirmation_started);
    {
        struct df_gvs_call_control_status status;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
            &control, &session, &deadline, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_CONFIRMATION, status.timer);
        TEST_ASSERT_INT_EQ(1000, (int)status.remaining_ms);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 500, &result));
    {
        struct df_gvs_call_control_status status;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
            &control, &session, &deadline, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_CONFIRMATION, status.timer);
        TEST_ASSERT_INT_EQ(500, (int)status.remaining_ms);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline,
        DF_GVS_CALL_CONFIRM_TIMEOUT_MS, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_expired);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    {
        struct df_gvs_call_control_status status;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
            &control, &session, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_LOCAL_HANGUP, status.end_reason);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_CONFIRMATION_TIMEOUT,
            status.fault);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
        TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
            &control, &session, &deadline, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
        TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline,
        DF_GVS_CALL_CONFIRM_TIMEOUT_MS + 1U, &result));
    TEST_ASSERT_INT_EQ(0, result.runtime.acknowledgement_expired);
}

void test_gvs_call_control_records_local_hangup_snapshot(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 12,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_call_control_status status;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 0, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_hangup(
        &control, &session, 12, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING, control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
    length = control_reply(packet, local, session.peer, 0x82, NULL, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
        &control, &session, &deadline, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_LOCAL_HANGUP, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
    TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
    length = control_reply(packet, session.peer, local, 0x02,
        (const uint8_t[]){1}, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 11, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_LOCAL_HANGUP, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
    TEST_ASSERT_INT_EQ(12, (int)status.last_generation);
    TEST_ASSERT_INT_EQ(0, memcmp(status.last_station, session.peer, 6));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
    TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
}

void test_gvs_call_control_preserves_terminal_fault_over_late_dispatch_cancel(void) {
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 14,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_call_control_status status;
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_hangup(
        &control, &session, 14, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(
        &deadline, &session, 0, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_SESSION_TIMEOUT,
        control.terminal.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, control.fault);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_CANCELLED, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
}

void test_gvs_call_control_preemption_keeps_new_generation_fault_clear(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t old[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t next_peer[6] = {0x32, 2, 1, 0, 1, 0};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_call_control_status status;
    uint8_t packet[64];
    size_t length;

    memcpy(session.peer, old, 6);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 0));
    length = control_reply(packet, local, next_peer, 0x01, NULL, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 1, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.receive.preempted_session);
    TEST_ASSERT_INT_EQ(8, (int)session.generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 1, &result));
    TEST_ASSERT_INT_EQ(1, result.dispatch_cancelled);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
        &control, &session, &deadline, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_NONE, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PREEMPTED,
        control.terminal.end_reason);
    TEST_ASSERT_INT_EQ(7, (int)status.last_generation);
}

void test_gvs_call_control_rejects_late_ack_without_overwriting_terminal(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 9,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_hangup(
        &control, &session, 9, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    length = control_reply(packet, local, session.peer, 0x02,
        (const uint8_t[]){1}, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 1, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PEER_HANGUP,
        control.terminal.end_reason);
    length = control_reply(packet, local, session.peer, 0x82, NULL, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 2, &result));
    TEST_ASSERT_INT_EQ(1, result.runtime.acknowledgement_rejected);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PEER_HANGUP,
        control.terminal.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, control.fault);
    TEST_ASSERT_INT_EQ(9, (int)control.terminal.generation);
}

void test_gvs_call_control_status_ignores_stale_generation_deadline(void) {
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 8};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(
        &deadline, &session, 0, 100));
    session.generation = 9;
    control.acknowledgement.state = DF_GVS_CALL_ACK_WAITING;
    control.acknowledgement.command.session_generation = 8;
    control.acknowledgement.deadline_ms = 9999;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status_with_deadline(
        &control, &session, &deadline, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMER_NONE, status.timer);
    TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
}

void test_gvs_call_control_records_terminal_timeout_snapshot(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 4,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_call_control_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(
        &deadline, &session, 0, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_SESSION_TIMEOUT, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
    TEST_ASSERT_INT_EQ(4, (int)status.last_generation);
    TEST_ASSERT_INT_EQ(0, (int)status.remaining_ms);
}

void test_gvs_call_control_records_peer_hangup_and_clears_timer(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 8,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;
    struct df_gvs_call_control_status status;
    uint8_t packet[64];
    const uint8_t payload[1] = {1};
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&deadline, &session, 0, 100));
    length = control_reply(packet, local, session.peer, 0x02, payload, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 10, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(0, deadline.armed);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PEER_HANGUP, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
    TEST_ASSERT_INT_EQ(8, (int)status.last_generation);
}

void test_gvs_call_control_new_runtime_has_empty_diagnostics(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_TALKING, .generation = 11,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_status status;
    struct df_gvs_call_control_result result;
    uint8_t packet[64];
    size_t length;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    length = control_reply(packet, local, session.peer, 0x02,
        (const uint8_t[]){1}, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 1, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PEER_HANGUP, status.end_reason);
    TEST_ASSERT_INT_EQ(11, (int)status.last_generation);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 2, df_gvs_placeholder_header_fields, NULL));
    session.state = DF_GVS_IDLE;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_NONE, status.end_reason);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, status.fault);
    TEST_ASSERT_INT_EQ(0, (int)status.last_generation);
}

void test_gvs_call_control_rejects_busy_and_stale_submission(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_call_control control;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_submit_answer(
        &control, &session, 6, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_call_control_submit_hangup(
        &control, &session, 7, local, 1, 0));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_QUEUED, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_EMPTY,
                       control.acknowledgement.state);
}

void test_gvs_call_control_bounds_failed_simulated_delivery(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7,
        .peer = {0x32, 2, 1, 0, 1, 0}};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_result result;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, reject_control_header, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 0, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 100, &result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, control.dispatch.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 200, &result));
    TEST_ASSERT_INT_EQ(1, result.dispatch_failed);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAILED, control.dispatch.state);
    TEST_ASSERT_INT_EQ(3, control.dispatch.attempts);
    {
        struct df_gvs_call_control_status status;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
            &control, &session, &status));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_DISPATCH_FAILED, status.fault);
    }
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_EMPTY,
                       control.acknowledgement.state);
    TEST_ASSERT_INT_EQ(0, control.sender.length);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_step(
        &control, &session, local, &deadline, 201, &result));
    TEST_ASSERT_INT_EQ(0, result.dispatch_failed);
}

void test_gvs_call_control_rejects_receive_before_submission_time(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t peer[6] = {0x32, 2, 1, 0, 1, 0};
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7};
    struct df_gvs_deadline deadline = {0};
    struct df_gvs_call_control control, before_control;
    struct df_gvs_session before_session;
    struct df_gvs_deadline before_deadline;
    struct df_gvs_call_control_result result = {.frame_ready = true};
    uint8_t packet[64];
    size_t length;

    memcpy(session.peer, peer, sizeof(peer));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(
        &control, &session, 7, local, 8303, 8302, 120, 100));
    length = control_reply(packet, local, peer, 0x02, NULL, 0);
    before_control = control;
    before_session = session;
    before_deadline = deadline;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_control_receive(
        &control, packet, length, local, &session, &deadline, 50, &result));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_control, &control, sizeof(control)));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_session, &session, sizeof(session)));
    TEST_ASSERT_INT_EQ(0, memcmp(&before_deadline, &deadline, sizeof(deadline)));
    TEST_ASSERT_INT_EQ(1, result.frame_ready);
}

void test_gvs_call_control_exposes_public_status(void) {
    struct df_gvs_session session = {.state = DF_GVS_RINGING, .generation = 7};
    struct df_gvs_call_control control;
    struct df_gvs_call_control_status status;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(
        &control, 0, df_gvs_placeholder_header_fields, NULL));
    control.dispatch.state = DF_GVS_CALL_RETRY;
    control.dispatch.command.type = DF_GVS_CALL_COMMAND_ANSWER;
    control.dispatch.attempts = 2;
    control.acknowledgement.state = DF_GVS_CALL_ACK_EMPTY;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_status(
        &control, &session, &status));
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, status.session_state);
    TEST_ASSERT_INT_EQ(7, status.session_generation);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_ANSWER, status.command_type);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY, status.dispatch_state);
    TEST_ASSERT_INT_EQ(2, status.attempts);
    TEST_ASSERT_INT_EQ(0, strcmp("ringing",
        df_gvs_session_state_name(status.session_state)));
    TEST_ASSERT_INT_EQ(0, strcmp("answer",
        df_gvs_call_command_type_name(status.command_type)));
    TEST_ASSERT_INT_EQ(0, strcmp("retry",
        df_gvs_call_dispatch_state_name(status.dispatch_state)));
    TEST_ASSERT_INT_EQ(0, strcmp("empty",
        df_gvs_call_ack_state_name(status.acknowledgement_state)));
    TEST_ASSERT_INT_EQ(1, df_gvs_session_state_name(
        (enum df_gvs_session_state)99) == NULL);
}
