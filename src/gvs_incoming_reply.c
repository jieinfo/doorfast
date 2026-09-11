#include "gvs_incoming_reply.h"

#include <string.h>

static bool df_gvs_nonzero_address(const uint8_t address[6]) {
    static const uint8_t zero[6] = {0};

    return address != NULL && memcmp(address, zero, sizeof(zero)) != 0;
}

int df_gvs_incoming_reply_prepare(
    const struct df_gvs_receive_result *observed,
    const struct df_gvs_session *session, uint64_t expected_generation,
    const uint8_t local[6], uint16_t media_port,
    struct df_gvs_incoming_reply *reply) {
    struct df_gvs_incoming_reply next = {0};

    if (reply == NULL) {
        return DF_ERR_INVALID;
    }
    memset(reply, 0, sizeof(*reply));
    if (observed == NULL || session == NULL || !observed->accepted_call ||
        session->state != DF_GVS_RINGING || expected_generation == 0U ||
        expected_generation != session->generation || media_port == 0U ||
        !df_gvs_nonzero_address(local) ||
        !df_gvs_nonzero_address(session->peer)) {
        return DF_ERR_INVALID;
    }
    next.valid = true;
    memcpy(next.destination, session->peer, sizeof(next.destination));
    memcpy(next.source, local, sizeof(next.source));
    next.session_generation = expected_generation;
    next.media_port = media_port;
    *reply = next;
    return DF_OK;
}

int df_gvs_incoming_reply_serialize(
    const struct df_gvs_incoming_reply *reply, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context) {
    uint8_t payload[DF_GVS_INCOMING_REPLY_PAYLOAD_SIZE];

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (reply == NULL || !reply->valid || reply->session_generation == 0U ||
        reply->media_port == 0U || !df_gvs_nonzero_address(reply->destination) ||
        !df_gvs_nonzero_address(reply->source)) {
        return DF_ERR_INVALID;
    }
    /* Recovered from TalkBackBusiness -> protocol.c sendCallReply().
     * 0x1e and the final 0x01 are preserved observed legacy constants; their
     * wider semantics are deliberately not inferred here. */
    payload[0] = 0x01;
    payload[1] = 0x00;
    payload[2] = 0x02;
    payload[3] = (uint8_t)(reply->media_port >> 8U);
    payload[4] = (uint8_t)(reply->media_port & 0xffU);
    payload[5] = 0x1e;
    payload[6] = 0x01;
    return df_gvs_control_serialize(
        output, capacity, output_length, reply->destination, reply->source,
        0x03, 0x81, payload, sizeof(payload), provide_fields, fields_context);
}
