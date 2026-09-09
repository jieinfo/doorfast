#include "gvs_memory_sender.h"

#include <limits.h>
#include <string.h>

#include "gvs_frame.h"

_Static_assert(DF_GVS_PEER_REPLY_FRAME_SIZE ==
                   DF_GVS_CONTROL_HEADER_SIZE + 6U,
               "peer reply frame size must include the complete payload");

int df_gvs_placeholder_header_fields(
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context) {
    (void)context;
    if (random_code == NULL || encryption_code == NULL) {
        return DF_ERR_INVALID;
    }
    memset(random_code, 0, DF_GVS_HEADER_FIELD_SIZE);
    memset(encryption_code, 0, DF_GVS_HEADER_FIELD_SIZE);
    return DF_OK;
}

int df_gvs_memory_sender_init(struct df_gvs_memory_sender *sender,
                              const uint8_t source[6],
                              df_gvs_header_fields_fn provide_fields,
                              void *fields_context) {
    if (sender == NULL || source == NULL || provide_fields == NULL) {
        return DF_ERR_INVALID;
    }
    memset(sender, 0, sizeof(*sender));
    memcpy(sender->source, source, sizeof(sender->source));
    sender->provide_fields = provide_fields;
    sender->fields_context = fields_context;
    return DF_OK;
}

enum df_gvs_send_attempt_result df_gvs_memory_send_attempt(
    const struct df_gvs_reply_queue_entry *entry, unsigned attempt,
    uint64_t completion_id, void *context) {
    struct df_gvs_memory_sender *sender = context;
    struct df_gvs_memory_frame_record next = {0};
    struct df_gvs_frame frame;
    struct df_event event;
    uint8_t expected_payload[6] = {0};

    if (entry == NULL || sender == NULL || attempt == 0U ||
        completion_id == 0U || sender->record.generation == UINT64_MAX) {
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    memcpy(expected_payload, entry->reply.request_data,
           sizeof(entry->reply.request_data));
    if (df_gvs_peer_reply_serialize(
            &entry->reply, sender->source, next.bytes, sizeof(next.bytes),
            &next.length, sender->provide_fields,
            sender->fields_context) != DF_OK ||
        next.length != DF_GVS_PEER_REPLY_FRAME_SIZE ||
        df_gvs_frame_parse(next.bytes, next.length, &frame, &event) != DF_OK ||
        memcmp(frame.destination, entry->reply.target,
               sizeof(frame.destination)) != 0 ||
        memcmp(frame.source, sender->source, sizeof(frame.source)) != 0 ||
        frame.family != 0x07 || frame.opcode != 0x81 ||
        frame.payload_length != sizeof(expected_payload) ||
        memcmp(frame.payload, expected_payload, sizeof(expected_payload)) != 0) {
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    next.valid = true;
    next.attempt = attempt;
    next.completion_id = completion_id;
    next.generation = sender->record.generation + 1U;
    sender->record = next;
    return DF_GVS_SEND_ATTEMPT_SUCCESS;
}
