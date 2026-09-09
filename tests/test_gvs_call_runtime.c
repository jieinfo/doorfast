#include <string.h>
#include "gvs_call_runtime.h"
#include "test.h"

static size_t runtime_frame(uint8_t out[64], const uint8_t dst[6],
    const uint8_t src[6], uint8_t opcode, const uint8_t *payload, size_t n) {
    static const uint8_t magic[10] = {
        'G', 'V', 'S', 'G', 'V', 'S', 0xa5, 0xa5, 0xa5, 0xa5
    };

    memset(out, 0, 64);
    memcpy(out, magic, 10);
    memcpy(out + 10, dst, 6);
    memcpy(out + 16, src, 6);
    out[38] = 3;
    out[39] = opcode;
    out[40] = (uint8_t)n;
    if (n != 0) {
        memcpy(out + 42, payload, n);
    }
    return 42 + n;
}

void test_gvs_call_runtime_confirms_before_talking_transition(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t ask[7]={2,0x20,0x6f,0,0x20,0x6e,0x78};
    const uint8_t reply[7]={0,0x20,0x6f,0,0x20,0x6e,0x1e};
    struct df_gvs_session s={.state=DF_GVS_RINGING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    struct df_gvs_deadline deadline={0};
    struct df_gvs_call_dispatch d={.state=DF_GVS_CALL_SENT};
    struct df_gvs_call_ack ack;
    struct df_gvs_receive_result seed;
    struct df_gvs_call_runtime_result result;
    uint8_t packet[64]; size_t n;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_answer(
        &s,9,local,8303,8302,120,&d.command));
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    n=runtime_frame(packet,s.peer,local,3,ask,7);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_receive_datagram(
        packet,n,local,&s,&deadline,10,&seed));
    n=runtime_frame(packet,local,s.peer,0x83,reply,7);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_runtime_receive(
        &ack,packet,n,local,&s,&deadline,20,&result));
    TEST_ASSERT_INT_EQ(1,result.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(1,result.receive.talking_transition);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CONFIRMED,ack.state);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING,s.state);
}

void test_gvs_call_runtime_swallows_wrong_answer(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t reply[7]={0,0x20,0x70,0,0x20,0x6e,0x1e};
    struct df_gvs_session s={.state=DF_GVS_RINGING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    struct df_gvs_deadline deadline={0};
    struct df_gvs_call_dispatch d={.state=DF_GVS_CALL_SENT};
    struct df_gvs_call_ack ack;
    struct df_gvs_call_runtime_result result;
    uint8_t packet[64]; size_t n;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_answer(
        &s,9,local,8303,8302,120,&d.command));
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    n=runtime_frame(packet,local,s.peer,0x83,reply,7);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_runtime_receive(
        &ack,packet,n,local,&s,&deadline,20,&result));
    TEST_ASSERT_INT_EQ(1,result.acknowledgement_rejected);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING,ack.state);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING,s.state);
}

void test_gvs_call_runtime_confirms_hangup_reply_without_ending_session(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    struct df_gvs_session s={.state=DF_GVS_TALKING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    struct df_gvs_deadline deadline={0};
    struct df_gvs_call_dispatch d={.state=DF_GVS_CALL_SENT};
    struct df_gvs_call_ack ack;
    struct df_gvs_call_runtime_result result;
    uint8_t packet[64]; size_t n;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_hangup(
        &s,9,local,1,&d.command));
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    n=runtime_frame(packet,local,s.peer,0x82,NULL,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_runtime_receive(
        &ack,packet,n,local,&s,&deadline,20,&result));
    TEST_ASSERT_INT_EQ(1,result.acknowledgement_confirmed);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING,s.state);
}

void test_gvs_call_runtime_cancels_ack_after_preemption(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t incoming[6]={0x32,2,1,0,2,0};
    struct df_gvs_session s={.state=DF_GVS_RINGING,.generation=9,
        .peer={0x61,2,1,1,1,2}};
    struct df_gvs_deadline deadline={0};
    struct df_gvs_call_dispatch d={.state=DF_GVS_CALL_SENT};
    struct df_gvs_call_ack ack;
    struct df_gvs_call_runtime_result result;
    uint8_t packet[64]; size_t n;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_answer(
        &s,9,local,8303,8302,120,&d.command));
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    n=runtime_frame(packet,local,incoming,1,NULL,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_runtime_receive(
        &ack,packet,n,local,&s,&deadline,20,&result));
    TEST_ASSERT_INT_EQ(1,result.receive.preempted_session);
    TEST_ASSERT_INT_EQ(1,result.acknowledgement_cancelled);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CANCELLED,ack.state);
    TEST_ASSERT_INT_EQ(10,(int)s.generation);
    TEST_ASSERT_INT_EQ(0,memcmp(s.peer,incoming,6));
}
