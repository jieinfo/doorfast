#ifndef DOORFAST_GVS_MEMORY_SENDER_H
#define DOORFAST_GVS_MEMORY_SENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_send_transaction.h"
#include "gvs_serialize.h"

#define DF_GVS_PEER_REPLY_FRAME_SIZE 48U

struct df_gvs_memory_frame_record {
    bool valid;
    uint8_t bytes[DF_GVS_PEER_REPLY_FRAME_SIZE];
    size_t length;
    unsigned attempt;
    uint64_t completion_id;
    uint64_t generation;
};

struct df_gvs_memory_sender {
    uint8_t source[6];
    df_gvs_header_provider_fn provide_fields;
    void *fields_context;
    struct df_gvs_memory_frame_record record;
};

int df_gvs_placeholder_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context);
int df_gvs_memory_sender_init(struct df_gvs_memory_sender *sender,
                              const uint8_t source[6],
                              df_gvs_header_provider_fn provide_fields,
                              void *fields_context);
enum df_gvs_send_attempt_result df_gvs_memory_send_attempt(
    const struct df_gvs_reply_queue_entry *entry, unsigned attempt,
    uint64_t completion_id, void *context);

#endif
