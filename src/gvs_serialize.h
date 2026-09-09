#ifndef DOORFAST_GVS_SERIALIZE_H
#define DOORFAST_GVS_SERIALIZE_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_presence.h"

#define DF_GVS_CONTROL_HEADER_SIZE 42U
#define DF_GVS_HEADER_FIELD_SIZE 8U

typedef int (*df_gvs_header_fields_fn)(
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context);

int df_gvs_control_serialize(
    uint8_t *output, size_t capacity, size_t *output_length,
    const uint8_t destination[6], const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, uint16_t payload_length,
    df_gvs_header_fields_fn provide_fields, void *fields_context);

int df_gvs_presence_action_serialize(
    const struct df_gvs_presence_action *action, const uint8_t source[6],
    uint16_t sync_version, uint8_t *output, size_t capacity,
    size_t *output_length, df_gvs_header_fields_fn provide_fields,
    void *fields_context);
int df_gvs_peer_reply_serialize(
    const struct df_gvs_peer_reply *reply, const uint8_t source[6],
    uint8_t *output, size_t capacity, size_t *output_length,
    df_gvs_header_fields_fn provide_fields, void *fields_context);

#endif
