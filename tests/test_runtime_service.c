#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "runtime_service.h"
#include "test.h"

struct delay_trace {
    unsigned values[20];
    size_t count;
    bool fail_second;
};

static int record_delay_slice(unsigned delay_ms, void *context) {
    struct delay_trace *trace = context;

    if (trace == NULL || trace->count >= 20U) {
        return DF_ERR_INVALID;
    }
    trace->values[trace->count++] = delay_ms;
    if (trace->fail_second && trace->count == 2U) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

void test_runtime_delay_is_pumped_in_bounded_slices(void) {
    struct delay_trace trace = {0};

    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_runtime_pump_delay(1000, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(4, (int)trace.count);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[0]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[1]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[2]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[3]);

    memset(&trace, 0, sizeof(trace));
    TEST_ASSERT_INT_EQ(
        DF_OK, df_runtime_pump_delay(251, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[0]);
    TEST_ASSERT_INT_EQ(1, (int)trace.values[1]);
}

void test_runtime_delay_rejects_invalid_input_and_stops_on_failure(void) {
    struct delay_trace trace = {.fail_second = true};

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_pump_delay(0, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_runtime_pump_delay(250, 0, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_runtime_pump_delay(250, 250, NULL, &trace));
    TEST_ASSERT_INT_EQ(
        DF_ERR_IO,
        df_runtime_pump_delay(500, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(2, (int)trace.count);
}
