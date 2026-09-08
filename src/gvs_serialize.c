#include "gvs_serialize.h"

#include <string.h>

static const uint8_t df_gvs_control_magic[10] = {
    'G', 'V', 'S', 'G', 'V', 'S', 0xA5, 0xA5, 0xA5, 0xA5,
};

int df_gvs_control_serialize(
    uint8_t *output, size_t capacity, size_t *output_length,
    const uint8_t destination[6], const uint8_t source[6], uint8_t family,
    uint8_t opcode, const uint8_t *payload, uint16_t payload_length,
    df_gvs_header_fields_fn provide_fields, void *fields_context) {
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE];
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE];
    size_t required = DF_GVS_CONTROL_HEADER_SIZE + (size_t)payload_length;

    if (output_length != NULL) {
        *output_length = 0;
    }
    if (output == NULL || output_length == NULL || destination == NULL ||
        source == NULL || provide_fields == NULL ||
        (payload_length > 0U && payload == NULL) || capacity < required ||
        provide_fields(random_code, encryption_code, fields_context) != DF_OK) {
        return DF_ERR_INVALID;
    }

    memcpy(output, df_gvs_control_magic, sizeof(df_gvs_control_magic));
    memcpy(output + 10, destination, 6);
    memcpy(output + 16, source, 6);
    memcpy(output + 22, random_code, sizeof(random_code));
    memcpy(output + 30, encryption_code, sizeof(encryption_code));
    output[38] = family;
    output[39] = opcode;
    output[40] = (uint8_t)(payload_length & 0xFFU);
    output[41] = (uint8_t)(payload_length >> 8U);
    if (payload_length > 0U) {
        memcpy(output + DF_GVS_CONTROL_HEADER_SIZE, payload, payload_length);
    }
    *output_length = required;
    return DF_OK;
}

int df_gvs_presence_action_serialize(
    const struct df_gvs_presence_action *action, const uint8_t source[6],
    uint16_t sync_version, uint8_t *output, size_t capacity,
    size_t *output_length, df_gvs_header_fields_fn provide_fields,
    void *fields_context) {
    uint8_t family;
    uint8_t opcode;
    uint8_t payload[3];
    uint16_t payload_length;

    if (output_length != NULL) {
        *output_length = 0;
    }
    if (action == NULL || source == NULL || output_length == NULL) {
        return DF_ERR_INVALID;
    }
    switch (action->type) {
    case DF_GVS_PRESENCE_PEER_PROBE:
        family = 0x07;
        opcode = 0x01;
        payload[0] = 0x00;
        payload[1] = 0x01;
        payload_length = 2;
        break;
    case DF_GVS_PRESENCE_SYNC_ASK_ACTION:
        if (action->round < 1U || action->round > 3U) {
            return DF_ERR_INVALID;
        }
        family = 0x91;
        opcode = 0x01;
        payload[0] = (uint8_t)(sync_version & 0xFFU);
        payload[1] = (uint8_t)(sync_version >> 8U);
        payload[2] = (uint8_t)action->round;
        payload_length = 3;
        break;
    case DF_GVS_PRESENCE_SYNC_VERSION_ASK:
        family = 0x91;
        opcode = 0x02;
        payload_length = 0;
        break;
    case DF_GVS_PRESENCE_PEER_ONLINE:
    case DF_GVS_PRESENCE_PEER_OFFLINE:
    case DF_GVS_PRESENCE_PERIODIC_SYNC:
    default:
        return DF_ERR_INVALID;
    }
    return df_gvs_control_serialize(
        output, capacity, output_length, action->target, source, family, opcode,
        payload_length == 0U ? NULL : payload, payload_length, provide_fields,
        fields_context);
}
