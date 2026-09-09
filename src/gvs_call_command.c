#include "gvs_call_command.h"

#include <string.h>

static bool df_gvs_call_command_session_active(
    const struct df_gvs_session *session) {
    return session->state == DF_GVS_PREVIEW ||
           session->state == DF_GVS_RINGING ||
           session->state == DF_GVS_TALKING;
}

static int df_gvs_call_command_begin(
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], enum df_gvs_call_command_type type,
    uint8_t opcode, struct df_gvs_call_command *command) {
    static const uint8_t zero_address[6] = {0};

    if (command == NULL) {
        return DF_ERR_INVALID;
    }
    memset(command, 0, sizeof(*command));
    if (session == NULL || local == NULL || expected_generation == 0U ||
        expected_generation != session->generation ||
        memcmp(local, zero_address, sizeof(zero_address)) == 0 ||
        memcmp(session->peer, zero_address, sizeof(zero_address)) == 0) {
        return DF_ERR_INVALID;
    }
    command->type = type;
    command->opcode = opcode;
    command->session_generation = expected_generation;
    memcpy(command->destination, session->peer,
           sizeof(command->destination));
    memcpy(command->source, local, sizeof(command->source));
    return DF_OK;
}

int df_gvs_call_command_prepare_answer(
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint16_t primary_media_port,
    uint16_t secondary_media_port, uint8_t duration_seconds,
    struct df_gvs_call_command *command) {
    if (df_gvs_call_command_begin(
            session, expected_generation, local, DF_GVS_CALL_COMMAND_ANSWER,
            0x03, command) != DF_OK || session->state != DF_GVS_RINGING ||
        primary_media_port == 0U || secondary_media_port == 0U ||
        duration_seconds == 0U) {
        if (command != NULL) {
            memset(command, 0, sizeof(*command));
        }
        return DF_ERR_INVALID;
    }
    command->payload[0] = 0x02;
    command->payload[1] = (uint8_t)(primary_media_port >> 8U);
    command->payload[2] = (uint8_t)(primary_media_port & 0xFFU);
    command->payload[3] = 0x00;
    command->payload[4] = (uint8_t)(secondary_media_port >> 8U);
    command->payload[5] = (uint8_t)(secondary_media_port & 0xFFU);
    command->payload[6] = duration_seconds;
    command->payload_length = sizeof(command->payload);
    command->valid = true;
    return DF_OK;
}

int df_gvs_call_command_prepare_hangup(
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint8_t reason,
    struct df_gvs_call_command *command) {
    if (df_gvs_call_command_begin(
            session, expected_generation, local, DF_GVS_CALL_COMMAND_HANGUP,
            0x02, command) != DF_OK ||
        !df_gvs_call_command_session_active(session)) {
        if (command != NULL) {
            memset(command, 0, sizeof(*command));
        }
        return DF_ERR_INVALID;
    }
    command->payload[0] = reason;
    command->payload_length = 1U;
    command->valid = true;
    return DF_OK;
}

int df_gvs_call_command_serialize(
    const struct df_gvs_call_command *command, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context) {
    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (command == NULL || !command->valid || output_length == NULL ||
        command->session_generation == 0U ||
        (command->type != DF_GVS_CALL_COMMAND_ANSWER &&
         command->type != DF_GVS_CALL_COMMAND_HANGUP) ||
        (command->opcode != 0x03 && command->opcode != 0x02) ||
        (command->type == DF_GVS_CALL_COMMAND_ANSWER &&
         (command->opcode != 0x03 || command->payload_length != 7U)) ||
        (command->type == DF_GVS_CALL_COMMAND_HANGUP &&
         (command->opcode != 0x02 || command->payload_length != 1U))) {
        return DF_ERR_INVALID;
    }
    return df_gvs_control_serialize(
        output, capacity, output_length, command->destination,
        command->source, 0x03, command->opcode, command->payload,
        command->payload_length, provide_fields, fields_context);
}
