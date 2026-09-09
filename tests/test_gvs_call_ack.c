#include <string.h>
#include "gvs_call_ack.h"
#include "test.h"

static struct df_gvs_session ack_session(void) {
    struct df_gvs_session s={.state=DF_GVS_RINGING,.generation=9,
        .peer={0x32,2,1,0,1,0}};
    return s;
}
static struct df_gvs_call_dispatch sent_answer(struct df_gvs_session *s,
    const uint8_t local[6]) {
    struct df_gvs_call_dispatch d;
    df_gvs_call_dispatch_init(&d,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_answer(
        s,9,local,8303,8302,120,&d.command));
    d.state=DF_GVS_CALL_SENT;
    return d;
}

void test_gvs_call_ack_confirms_matching_answer(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t payload[7]={0,0x20,0x6f,0,0x20,0x6e,0x1e};
    struct df_gvs_session s=ack_session(), before=s;
    struct df_gvs_call_dispatch d=sent_answer(&s,local);
    struct df_gvs_call_ack ack;
    struct df_gvs_frame f={.family=3,.opcode=0x83,.payload=payload,
        .payload_length=7};
    memcpy(f.source,s.peer,6); memcpy(f.destination,local,6);
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING,ack.state);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_observe(&ack,&s,local,&f,500));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CONFIRMED,ack.state);
    TEST_ASSERT_INT_EQ(0,memcmp(&s,&before,sizeof(s)));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_ack_observe(&ack,&s,local,&f,501));
}

void test_gvs_call_ack_rejects_unrelated_and_expires(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    uint8_t payload[7]={0,0x20,0x6f,0,0x20,0x6e,0x1e};
    struct df_gvs_session s=ack_session();
    struct df_gvs_call_dispatch d=sent_answer(&s,local);
    struct df_gvs_call_ack ack,before;
    struct df_gvs_frame f={.family=3,.opcode=0x83,.payload=payload,
        .payload_length=7};
    memcpy(f.source,s.peer,6); memcpy(f.destination,local,6);
    df_gvs_call_ack_init(&ack,0);
    d.command.opcode=2;
    before=ack;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_ack_begin(
        &ack,&d,&s,local,0,1000));
    TEST_ASSERT_INT_EQ(0,memcmp(&ack,&before,sizeof(ack)));
    d.command.opcode=3;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    f.source[5]=1; before=ack;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_ack_observe(&ack,&s,local,&f,100));
    TEST_ASSERT_INT_EQ(0,memcmp(&ack,&before,sizeof(ack)));
    f.source[5]=0; payload[2]=0x70;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_ack_observe(&ack,&s,local,&f,200));
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_tick(&ack,&s,local,1000));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_EXPIRED,ack.state);
}

void test_gvs_call_ack_confirms_hangup_and_cancels_stale(void) {
    const uint8_t local[6]={0x61,2,1,1,1,1};
    struct df_gvs_session s=ack_session();
    struct df_gvs_call_dispatch d;
    struct df_gvs_call_ack ack;
    struct df_gvs_frame f={.family=3,.opcode=0x82,.payload=NULL,
        .payload_length=0};
    df_gvs_call_dispatch_init(&d,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_command_prepare_hangup(
        &s,9,local,1,&d.command)); d.state=DF_GVS_CALL_SENT;
    memcpy(f.source,s.peer,6); memcpy(f.destination,local,6);
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_observe(&ack,&s,local,&f,10));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CONFIRMED,ack.state);
    df_gvs_call_ack_init(&ack,20);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,20,1000));
    s.generation++;
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_tick(&ack,&s,local,21));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CANCELLED,ack.state);
}

void test_gvs_call_ack_receives_complete_frame(void) {
    static const uint8_t magic[10]={'G','V','S','G','V','S',0xa5,0xa5,0xa5,0xa5};
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t payload[7]={0,0x20,0x6f,0,0x20,0x6e,0x1e};
    struct df_gvs_session s=ack_session();
    struct df_gvs_call_dispatch d=sent_answer(&s,local);
    struct df_gvs_call_ack ack,before;
    uint8_t packet[49]={0};
    memcpy(packet,magic,10); memcpy(packet+10,local,6);
    memcpy(packet+16,s.peer,6); packet[38]=3; packet[39]=0x83;
    packet[40]=7; memcpy(packet+42,payload,7);
    df_gvs_call_ack_init(&ack,0);
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_begin(&ack,&d,&s,local,0,1000));
    before=ack;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,df_gvs_call_ack_receive(
        &ack,&s,local,packet,48,10));
    TEST_ASSERT_INT_EQ(0,memcmp(&ack,&before,sizeof(ack)));
    TEST_ASSERT_INT_EQ(DF_OK,df_gvs_call_ack_receive(
        &ack,&s,local,packet,sizeof(packet),10));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_CONFIRMED,ack.state);
}
