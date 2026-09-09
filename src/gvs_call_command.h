#ifndef DOORFAST_GVS_CALL_COMMAND_H
#define DOORFAST_GVS_CALL_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_serialize.h"
#include "gvs_session.h"

#define DF_GVS_CALL_COMMAND_MAX_PAYLOAD_SIZE 7U
#define DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE \
    (DF_GVS_CONTROL_HEADER_SIZE + DF_GVS_CALL_COMMAND_MAX_PAYLOAD_SIZE)

enum df_gvs_call_command_type {
    DF_GVS_CALL_COMMAND_ANSWER = 1,
    DF_GVS_CALL_COMMAND_HANGUP,
};

struct df_gvs_call_command {
    bool valid;
    enum df_gvs_call_command_type type;
    uint8_t destination[6];
    uint8_t source[6];
    uint64_t session_generation;
    uint8_t opcode;
    uint8_t payload[DF_GVS_CALL_COMMAND_MAX_PAYLOAD_SIZE];
    uint16_t payload_length;
};

/* Prepares an offline, generation-bound command without mutating the session.
 * Port fields use network byte order inside the seven-byte 03/03 payload. */
int df_gvs_call_command_prepare_answer(
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint16_t primary_media_port,
    uint16_t secondary_media_port, uint8_t duration_seconds,
    struct df_gvs_call_command *command);

int df_gvs_call_command_prepare_hangup(
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint8_t reason,
    struct df_gvs_call_command *command);

int df_gvs_call_command_serialize(
    const struct df_gvs_call_command *command, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

#endif
