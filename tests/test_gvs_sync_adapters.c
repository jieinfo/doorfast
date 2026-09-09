#include <string.h>

#include "gvs_sync_adapters.h"
#include "test.h"

void test_gvs_sync_adapters_require_explicit_nonempty_sensitive_values(void) {
    struct df_gvs_sync_adapter_registry registry;
    struct df_gvs_sync_store store;
    const struct df_gvs_sync_adapter *adapter;

    df_gvs_sync_store_init(&store);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_sync_adapter_registry_init(&registry));
    TEST_ASSERT_INT_EQ(2, (int)registry.count);
    TEST_ASSERT_INT_EQ(0, (int)store.count);
    TEST_ASSERT_INT_EQ(0, (int)df_gvs_sync_adapter_enabled_count(&registry));

    adapter = df_gvs_sync_adapter_find(&registry, "sync_mini1_secretkey");
    TEST_ASSERT_INT_EQ(1, adapter != NULL);
    TEST_ASSERT_INT_EQ(1, adapter != NULL && adapter->sensitive);
    TEST_ASSERT_INT_EQ(0, adapter != NULL && adapter->enabled);
    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_sync_adapter_enable(&registry, &store,
                                   "sync_mini1_secretkey", ""));
    TEST_ASSERT_INT_EQ(0, (int)store.count);

    TEST_ASSERT_INT_EQ(
        DF_OK,
        df_gvs_sync_adapter_enable(&registry, &store,
                                   "sync_mini1_secretkey", "synthetic"));
    TEST_ASSERT_INT_EQ(1, (int)store.count);
    TEST_ASSERT_INT_EQ(1, (int)df_gvs_sync_adapter_enabled_count(&registry));
    TEST_ASSERT_INT_EQ(0, strcmp("sync_mini1_secretkey",
                                 store.entries[0].key));

    TEST_ASSERT_INT_EQ(
        DF_ERR_INVALID,
        df_gvs_sync_adapter_enable(&registry, &store, "unknown", "value"));
    TEST_ASSERT_INT_EQ(1, (int)store.count);
}
