#ifndef DOORFAST_GVS_INCOMING_REPLY_H
#define DOORFAST_GVS_INCOMING_REPLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gvs_receive.h"
#include "gvs_serialize.h"

#define DF_GVS_INCOMING_REPLY_PAYLOAD_SIZE 7U
#define DF_GVS_INCOMING_REPLY_FRAME_SIZE \
    (DF_GVS_CONTROL_HEADER_SIZE + DF_GVS_INCOMING_REPLY_PAYLOAD_SIZE)

struct df_gvs_incoming_reply {
    bool valid;
    uint8_t destination[6];
    uint8_t source[6];
    uint64_t session_generation;
    uint16_t media_port;
};

/* Convert one accepted 03/01 observation into the legacy indoor-station
 * 03/81 ringing reply. Repeated 03/01 observations intentionally create
 * repeated reply intents while remaining bound to the same session generation.
 * This function has no network side effects and does not mutate the session. */
int df_gvs_incoming_reply_prepare(
    const struct df_gvs_receive_result *observed,
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint16_t media_port,
    struct df_gvs_incoming_reply *reply);

int df_gvs_incoming_reply_serialize(
    const struct df_gvs_incoming_reply *reply, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);

#endif
