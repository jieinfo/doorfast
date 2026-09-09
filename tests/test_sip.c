#include <string.h>

#include "sip.h"
#include "test.h"

void test_sip_invite_and_bye(void) {
    const char invite[] =
        "INVITE sip:1001@172.16.10.2 SIP/2.0\r\n"
        "Call-ID: call-a\r\n\r\n";
    const char bye[] =
        "BYE sip:1001@172.16.10.2 SIP/2.0\r\n"
        "Call-ID: call-a\r\n\r\n";
    struct df_event event = {0};

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_sip_parse((const unsigned char *)invite, strlen(invite), &event));
    TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, event.type);
    TEST_ASSERT_INT_EQ(0, strcmp("call-a", event.call_id));

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_sip_parse((const unsigned char *)bye, strlen(bye), &event));
    TEST_ASSERT_INT_EQ(DF_EVENT_HANGUP, event.type);
}
