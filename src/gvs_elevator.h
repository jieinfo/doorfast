#ifndef DOORFAST_GVS_ELEVATOR_H
#define DOORFAST_GVS_ELEVATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_serialize.h"
#include "gvs_frame.h"

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

typedef int (*df_gvs_elevator_send_fn)(
    const struct df_gvs_elevator_request *, void *);

#define DF_GVS_ELEVATOR_MAX_ENTRIES 8U

enum df_gvs_elevator_motion {
    DF_GVS_ELEVATOR_FAULT = 0,
    DF_GVS_ELEVATOR_MOVING_UP = 1,
    DF_GVS_ELEVATOR_MOVING_DOWN = 2,
    DF_GVS_ELEVATOR_STOPPED = 3,
    DF_GVS_ELEVATOR_OTHER = 255,
};

struct df_gvs_elevator_entry {
    int8_t raw_floor;
    int16_t floor;
    uint8_t raw_state;
    enum df_gvs_elevator_motion motion;
};

struct df_gvs_elevator_status {
    bool valid;
    size_t count;
    size_t extension_length;
    struct df_gvs_elevator_entry entries[DF_GVS_ELEVATOR_MAX_ENTRIES];
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
int df_gvs_elevator_parse_status(
    const struct df_gvs_frame *frame, const uint8_t local[6],
    struct df_gvs_elevator_status *status);
const char *df_gvs_elevator_motion_name(enum df_gvs_elevator_motion motion);

#endif
