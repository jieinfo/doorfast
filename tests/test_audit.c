#include <string.h>

#include "audit.h"
#include "test.h"

void test_audit_format_is_redacted(void) {
    char output[128];
    struct df_event event = {.type = DF_EVENT_INCOMING_CALL, .call_id = "call-a"};

    df_audit_format(output, sizeof(output), &event, DF_DECISION_NOTIFY);
    TEST_ASSERT_INT_EQ(0, strcmp("event=IncomingCall decision=notify session=call-a", output));
}
