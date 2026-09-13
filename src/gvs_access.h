#ifndef DOORFAST_GVS_ACCESS_H
#define DOORFAST_GVS_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_frame.h"
#include "gvs_serialize.h"
#include "gvs_session.h"

#define DF_GVS_ACCESS_DIRECT_FRAME_SIZE 50U

struct df_gvs_access_request {
    bool valid;
    uint64_t session_generation;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t material[8];
};

enum df_gvs_access_result_state {
    DF_GVS_ACCESS_PROTOCOL_EMPTY,
    DF_GVS_ACCESS_PROTOCOL_WAITING,
    DF_GVS_ACCESS_PROTOCOL_COMPLETED,
    DF_GVS_ACCESS_PROTOCOL_REJECTED,
    DF_GVS_ACCESS_PROTOCOL_EXPIRED,
    DF_GVS_ACCESS_PROTOCOL_CANCELLED,
    DF_GVS_ACCESS_PROTOCOL_SEND_FAILED,
};

struct df_gvs_access_result {
    enum df_gvs_access_result_state state;
    uint64_t session_generation;
    uint64_t last_now_ms;
    uint64_t deadline_ms;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t raw_status;
    bool physical_result_confirmed;
};

int df_gvs_access_prepare_direct(
    const struct df_gvs_session *session, uint64_t session_generation,
    const uint8_t source[6], const uint8_t material[8],
    struct df_gvs_access_request *request);

int df_gvs_access_serialize_direct(
    const struct df_gvs_access_request *request, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

void df_gvs_access_result_init(struct df_gvs_access_result *result,
                               uint64_t now_ms);
int df_gvs_access_result_begin(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    uint64_t session_generation, const uint8_t local[6], uint64_t now_ms,
    uint64_t timeout_ms);
int df_gvs_access_result_observe(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    const uint8_t local[6], const struct df_gvs_frame *frame,
    uint64_t now_ms);
int df_gvs_access_result_tick(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    const uint8_t local[6], uint64_t now_ms);

typedef int (*df_gvs_access_send_fn)(const struct df_gvs_access_request *, void *);
struct df_gvs_access_control {
    struct df_gvs_access_result result;
    bool configured;
    uint8_t material[8];
    uint64_t attempted_generation;
    df_gvs_access_send_fn send;
    void *send_context;
};
int df_gvs_access_control_init(struct df_gvs_access_control *, const char *,
    uint64_t, df_gvs_access_send_fn, void *);
int df_gvs_access_control_submit(struct df_gvs_access_control *,
    const struct df_gvs_session *, uint64_t, const uint8_t [6], uint64_t);
const char *df_gvs_access_state_name(enum df_gvs_access_result_state);

#endif
