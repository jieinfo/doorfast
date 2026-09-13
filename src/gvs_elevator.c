#include "gvs_elevator.h"

#include <string.h>

#include "gvs_identity.h"

static int df_gvs_elevator_bcd_decode(uint8_t value, uint8_t *decoded) {
    uint8_t high = (uint8_t)(value >> 4U);
    uint8_t low = (uint8_t)(value & 0x0fU);

    if (decoded == NULL || high > 9U || low > 9U)
        return DF_ERR_INVALID;
    *decoded = (uint8_t)(high * 10U + low);
    return DF_OK;
}

static int df_gvs_elevator_target(
    const uint8_t local[6], uint8_t target[6]) {
    char host[DF_GVS_IPV4_TEXT_SIZE];

    if (local == NULL || target == NULL || local[0] != 0x61 ||
        df_gvs_identity_unicast_ip(local, host) != DF_OK)
        return DF_ERR_INVALID;
    target[0] = 0x35;
    target[1] = local[1];
    target[2] = local[2];
    target[3] = 0;
    target[4] = 1;
    target[5] = 0;
    return DF_OK;
}

int df_gvs_elevator_prepare_call(
    const uint8_t local[6], enum df_gvs_elevator_direction direction,
    struct df_gvs_elevator_request *request) {
    struct df_gvs_elevator_request next = {0};
    uint8_t floor;

    if (request != NULL)
        *request = next;
    if (local == NULL || request == NULL ||
        (direction != DF_GVS_ELEVATOR_DOWN &&
         direction != DF_GVS_ELEVATOR_UP) ||
        df_gvs_elevator_target(local, next.destination) != DF_OK ||
        df_gvs_elevator_bcd_decode(local[3], &floor) != DF_OK)
        return DF_ERR_INVALID;
    next.valid = true;
    memcpy(next.source, local, sizeof(next.source));
    next.opcode = 0x02;
    next.payload[0] = (uint8_t)direction;
    next.payload[1] = floor;
    next.payload[2] = local[3];
    next.payload[3] = local[4];
    next.payload_length = DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE;
    *request = next;
    return DF_OK;
}

int df_gvs_elevator_prepare_query(
    const uint8_t local[6], struct df_gvs_elevator_request *request) {
    struct df_gvs_elevator_request next = {0};

    if (request != NULL)
        *request = next;
    if (local == NULL || request == NULL ||
        df_gvs_elevator_target(local, next.destination) != DF_OK)
        return DF_ERR_INVALID;
    next.valid = true;
    memcpy(next.source, local, sizeof(next.source));
    next.opcode = 0x03;
    *request = next;
    return DF_OK;
}

int df_gvs_elevator_serialize(
    const struct df_gvs_elevator_request *request, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context) {
    uint8_t next[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
    size_t next_length = 0;
    size_t required;
    int status;

    if (output_length != NULL)
        *output_length = 0;
    if (request == NULL || output == NULL || output_length == NULL ||
        provide_fields == NULL || !request->valid ||
        !((request->opcode == 0x02 &&
           request->payload_length == DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE) ||
          (request->opcode == 0x03 && request->payload_length == 0U)))
        return DF_ERR_INVALID;
    required = DF_GVS_CONTROL_HEADER_SIZE + request->payload_length;
    if (capacity < required)
        return DF_ERR_INVALID;
    status = df_gvs_control_serialize(
        next, sizeof(next), &next_length, request->destination,
        request->source, 0x08, request->opcode,
        request->payload_length == 0U ? NULL : request->payload,
        request->payload_length, provide_fields, fields_context);
    if (status != DF_OK || next_length != required)
        return DF_ERR_INVALID;
    memcpy(output, next, next_length);
    *output_length = next_length;
    return DF_OK;
}
