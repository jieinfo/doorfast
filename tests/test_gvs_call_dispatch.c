#include <stdint.h>
#include <string.h>
#include "test.h"
#include "gvs_call_dispatch.h"
#include "gvs_memory_sender.h"

static enum df_gvs_send_attempt_result attempt(
    const struct df_gvs_call_command *command, unsigned n, uint64_t id,
    void *context) {
    (void)command; (void)n; (void)id;
    return *(enum df_gvs_send_attempt_result *)context;
}

void test_gvs_call_dispatch_lifecycle(void) {
    struct df_gvs_session s = {.state=DF_GVS_RINGING, .generation=7,
        .peer={0x32,2,1,0,1,0}};
    uint8_t local[6]={0x61,2,1,1,1,1};
    struct df_gvs_call_command c;
    struct df_gvs_call_dispatch d;
    enum df_gvs_send_attempt_result result=DF_GVS_SEND_ATTEMPT_PENDING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_command_prepare_answer(
        &s,7,local,8303,8302,120,&c));
    df_gvs_call_dispatch_init(&d,0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_call_dispatch_enqueue(&d,&c,0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,0,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENDING,d.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,250,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_RETRY,d.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,350,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_call_dispatch_complete(
        &d,&s,local,351,1,DF_GVS_SEND_ATTEMPT_SUCCESS));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_complete(
        &d,&s,local,351,2,DF_GVS_SEND_ATTEMPT_SUCCESS));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT,d.state);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING,s.state);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,352));
    s.generation++;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,352,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_CANCELLED,d.state);
    TEST_ASSERT_INT_EQ(0,d.attempts);
    s.generation = 7;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,353));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,353,attempt,&result));
    s.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_complete(
        &d,&s,local,354,d.active_id,DF_GVS_SEND_ATTEMPT_SUCCESS));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_CANCELLED,d.state);
}

void test_gvs_call_dispatch_bounds(void) {
    struct df_gvs_session s = {.state=DF_GVS_RINGING, .generation=7,
        .peer={0x32,2,1,0,1,0}};
    uint8_t local[6]={0x61,2,1,1,1,1};
    struct df_gvs_call_command c;
    struct df_gvs_call_dispatch d;
    enum df_gvs_send_attempt_result result=DF_GVS_SEND_ATTEMPT_FAILURE;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_command_prepare_hangup(&s,7,local,1,&c));
    df_gvs_call_dispatch_init(&d,0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,0,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,100,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,200,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAILED,d.state);
    TEST_ASSERT_INT_EQ(3,d.attempts);
    df_gvs_call_dispatch_init(&d,UINT64_MAX-250);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,UINT64_MAX-250));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,UINT64_MAX-250,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,UINT64_MAX-150,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMEOUT,d.state);
    df_gvs_call_dispatch_init(&d,0);
    result = DF_GVS_SEND_ATTEMPT_PENDING;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
    {
        const uint64_t times[] = {0,250,350,600,700,950};
        size_t i;
        for (i=0;i<sizeof(times)/sizeof(times[0]);i++)
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(
                &d,&s,local,times[i],attempt,&result));
    }
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMEOUT,d.state);
    TEST_ASSERT_INT_EQ(3,d.attempts);
    df_gvs_call_dispatch_init(&d,0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,250,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_TIMEOUT,d.state);
    TEST_ASSERT_INT_EQ(0,d.attempts);
    df_gvs_call_dispatch_init(&d,0);
    d.next_id = UINT64_MAX;
    result = DF_GVS_SEND_ATTEMPT_FAILURE;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,0,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(&d,&s,local,100,attempt,&result));
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAILED,d.state);
    {
        struct df_gvs_call_memory_sender sender = {
            .provide_fields = df_gvs_placeholder_header_fields
        };
        df_gvs_call_dispatch_init(&d,0);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_enqueue(&d,&c,0));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_dispatch_step(
            &d,&s,local,0,df_gvs_call_memory_attempt,&sender));
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_SENT,d.state);
        TEST_ASSERT_INT_EQ(43,sender.length);
        TEST_ASSERT_INT_EQ(3,sender.bytes[38]);
        TEST_ASSERT_INT_EQ(2,sender.bytes[39]);
        TEST_ASSERT_INT_EQ(1,sender.bytes[42]);
    }
}
