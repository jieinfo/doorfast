#ifndef DOORFAST_GVS_SYNC_H
#define DOORFAST_GVS_SYNC_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_serialize.h"

#define DF_GVS_SYNC_MAX_ENTRIES 64U
#define DF_GVS_SYNC_KEY_SIZE 64U
#define DF_GVS_SYNC_VALUE_SIZE 256U
#define DF_GVS_SYNC_CHUNK_ENTRIES 20U
#define DF_GVS_SYNC_MAX_PACKET_SIZE 1500U

struct df_gvs_sync_entry {
    char key[DF_GVS_SYNC_KEY_SIZE];
    char value[DF_GVS_SYNC_VALUE_SIZE];
};

struct df_gvs_sync_store {
    struct df_gvs_sync_entry entries[DF_GVS_SYNC_MAX_ENTRIES];
    size_t count;
};

void df_gvs_sync_store_init(struct df_gvs_sync_store *store);
int df_gvs_sync_store_register(struct df_gvs_sync_store *store,
                               const char *key, const char *initial_value);
int df_gvs_sync_store_update_local(struct df_gvs_sync_store *store,
                                   uint16_t *version, const char *key,
                                   const char *value);
size_t df_gvs_sync_periodic_chunk_count(const struct df_gvs_sync_store *store);

int df_gvs_sync_periodic_serialize(
    const struct df_gvs_sync_store *store, size_t chunk_index,
    const struct df_gvs_presence_action *action, const uint8_t source[6],
    uint16_t version, uint8_t *output, size_t capacity,
    size_t *output_length, df_gvs_header_provider_fn provide_fields,
    void *fields_context);

int df_gvs_sync_normal_serialize(
    const struct df_gvs_sync_store *store, const char *key,
    const uint8_t destination[6], const uint8_t source[6], uint16_t version,
    uint8_t *output, size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

int df_gvs_sync_receive(struct df_gvs_sync_store *store,
                        struct df_gvs_presence *presence,
                        const uint8_t *data, size_t length, uint64_t now_ms,
                        bool *resend_local);

#endif
