#include <limits.h>
#include <stddef.h>

#include "gvs_priority.h"
#include "test.h"

void test_gvs_priority_valid_matrix_and_unknown_categories(void) {
    uint8_t peer[6] = {0};
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const int type_cases[][2] = {{0x12, 1}, {0x31, 1}, {0x13, 0},
        {0x32, 0}, {0x62, 7}, {0x00, -1}, {0xff, -1}};
    for (size_t i = 0; i < sizeof(type_cases) / sizeof(type_cases[0]); ++i) {
        peer[0] = (uint8_t)type_cases[i][0];
        TEST_ASSERT_INT_EQ(type_cases[i][1], df_gvs_call_category(peer, local));
    }
    for (size_t i = 0; i < 6; ++i) peer[i] = local[i];
    peer[5] = 2;
    TEST_ASSERT_INT_EQ(4, df_gvs_call_category(peer, local));
    peer[4] = 2;
    TEST_ASSERT_INT_EQ(3, df_gvs_call_category(peer, local));
    TEST_ASSERT_INT_EQ(-1, df_gvs_call_category(NULL, local));
    TEST_ASSERT_INT_EQ(-1, df_gvs_call_category(peer, NULL));
    const int categories[] = {0, 1, 7, 3, 4};
    /* Rows=current, columns=incoming. Literal evidence-derived decisions. */
    const int expected[5][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {1, 1, 1, 0, 0},
        {1, 1, 1, 1, 0}
    };
    const int invalid[] = {INT_MIN, -1, 2, 5, 6, 8, INT_MAX};
    for (size_t row = 0; row < 5; ++row) {
        for (size_t col = 0; col < 5; ++col) {
            TEST_ASSERT_INT_EQ(expected[row][col],
                df_gvs_priority_compare(categories[row], categories[col]));
        }
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        for (size_t j = 0; j < 5; ++j) {
            TEST_ASSERT_INT_EQ(DF_GVS_PRIORITY_UNSUPPORTED,
                df_gvs_priority_compare(invalid[i], categories[j]));
            TEST_ASSERT_INT_EQ(DF_GVS_PRIORITY_UNSUPPORTED,
                df_gvs_priority_compare(categories[j], invalid[i]));
        }
        TEST_ASSERT_INT_EQ(DF_GVS_PRIORITY_UNSUPPORTED,
            df_gvs_priority_compare(invalid[i], invalid[i]));
    }
}
