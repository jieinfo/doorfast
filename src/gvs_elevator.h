#ifndef DOORFAST_GVS_ELEVATOR_H
#define DOORFAST_GVS_ELEVATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_serialize.h"

#define DF_GVS_ELEVATOR_CALL_FRAME_SIZE 46U
#define DF_GVS_ELEVATOR_QUERY_FRAME_SIZE 42U
#define DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE 4U

enum df_gvs_elevator_direction {
    DF_GVS_ELEVATOR_DOWN = 0,
    DF_GVS_ELEVATOR_UP = 1,
};

struct df_gvs_elevator_request {
    bool valid;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t opcode;
    uint8_t payload[DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE];
    uint16_t payload_length;
};

int df_gvs_elevator_prepare_call(
    const uint8_t local[6], enum df_gvs_elevator_direction direction,
    struct df_gvs_elevator_request *request);
int df_gvs_elevator_prepare_query(
    const uint8_t local[6], struct df_gvs_elevator_request *request);
int df_gvs_elevator_serialize(
    const struct df_gvs_elevator_request *request, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

#endif
