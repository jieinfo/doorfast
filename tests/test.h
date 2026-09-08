#ifndef DOORFAST_TEST_H
#define DOORFAST_TEST_H

#include <stdio.h>

extern int df_test_failure_count;

#define TEST_ASSERT_INT_EQ(expected, actual) \
    do { \
        int expected_value = (expected); \
        int actual_value = (actual); \
        if (expected_value != actual_value) { \
            fprintf(stderr, "%s:%d expected %d got %d\n", \
                    __FILE__, __LINE__, expected_value, actual_value); \
            df_test_failure_count++; \
        } \
    } while (0)

static inline int test_failures(void) {
    return df_test_failure_count;
}

#endif
