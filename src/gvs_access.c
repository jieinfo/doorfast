#include "gvs_access.h"

#include <limits.h>
#include <string.h>

static bool df_gvs_access_session_active(const struct df_gvs_session *session) {
    return session->state == DF_GVS_PREVIEW ||
           session->state == DF_GVS_RINGING ||
           session->state == DF_GVS_TALKING;
}

static bool df_gvs_access_address_valid(const uint8_t address[6]) {
    static const uint8_t zero[6] = {0};

    return memcmp(address, zero, sizeof(zero)) != 0;
}

static bool df_gvs_access_target_supported(const uint8_t address[6]) {
    return address[0] == 0x32 || address[0] == 0x13 || address[0] == 0x62;
}

static bool df_gvs_access_result_current(
    const struct df_gvs_access_result *result,
    const struct df_gvs_session *session, const uint8_t local[6]) {
    return result->session_generation == session->generation &&
           df_gvs_access_session_active(session) &&
           memcmp(result->destination, session->peer, 6) == 0 &&
           memcmp(result->source, local, 6) == 0;
}

int df_gvs_access_prepare_direct(
    const struct df_gvs_session *session, uint64_t session_generation,
    const uint8_t source[6], const uint8_t material[8],
    struct df_gvs_access_request *request) {
    struct df_gvs_access_request next = {0};

    if (request != NULL)
        *request = next;
    if (session == NULL || source == NULL || material == NULL ||
        request == NULL || session_generation == 0U ||
        session->generation != session_generation ||
        !df_gvs_access_session_active(session) ||
        !df_gvs_access_address_valid(source) ||
        !df_gvs_access_address_valid(session->peer) ||
        !df_gvs_access_target_supported(session->peer))
        return DF_ERR_INVALID;
    next.valid = true;
    next.session_generation = session_generation;
    memcpy(next.destination, session->peer, sizeof(next.destination));
    memcpy(next.source, source, sizeof(next.source));
    memcpy(next.material, material, sizeof(next.material));
    *request = next;
    return DF_OK;
}

int df_gvs_access_serialize_direct(
    const struct df_gvs_access_request *request, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context) {
    uint8_t next[DF_GVS_ACCESS_DIRECT_FRAME_SIZE];
    size_t next_length = 0;

    if (output_length != NULL)
        *output_length = 0;
    if (request == NULL || output == NULL || output_length == NULL ||
        !request->valid || capacity < sizeof(next) || provide_fields == NULL ||
        df_gvs_control_serialize(
            next, sizeof(next), &next_length, request->destination,
            request->source, 0x04, 0x09, request->material,
            sizeof(request->material), provide_fields, fields_context) != DF_OK ||
        next_length != sizeof(next))
        return DF_ERR_INVALID;
    next[40] = 0x0c;
    next[41] = 0x00;
    memcpy(output, next, sizeof(next));
    *output_length = sizeof(next);
    return DF_OK;
}

void df_gvs_access_result_init(struct df_gvs_access_result *result,
                               uint64_t now_ms) {
    if (result == NULL)
        return;
    memset(result, 0, sizeof(*result));
    result->last_now_ms = now_ms;
}

int df_gvs_access_result_begin(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    uint64_t session_generation, const uint8_t local[6], uint64_t now_ms,
    uint64_t timeout_ms) {
    struct df_gvs_access_result next;

    if (result == NULL || session == NULL || local == NULL ||
        result->state == DF_GVS_ACCESS_PROTOCOL_WAITING ||
        now_ms < result->last_now_ms || timeout_ms == 0U ||
        now_ms > UINT64_MAX - timeout_ms || session_generation == 0U ||
        session->generation != session_generation ||
        !df_gvs_access_session_active(session) ||
        !df_gvs_access_target_supported(session->peer) ||
        !df_gvs_access_address_valid(local))
        return DF_ERR_INVALID;
    next = *result;
    next.state = DF_GVS_ACCESS_PROTOCOL_WAITING;
    next.session_generation = session_generation;
    next.last_now_ms = now_ms;
    next.deadline_ms = now_ms + timeout_ms;
    next.raw_status = 0;
    next.physical_result_confirmed = false;
    memcpy(next.destination, session->peer, sizeof(next.destination));
    memcpy(next.source, local, sizeof(next.source));
    *result = next;
    return DF_OK;
}

int df_gvs_access_result_observe(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    const uint8_t local[6], const struct df_gvs_frame *frame,
    uint64_t now_ms) {
    if (result == NULL || session == NULL || local == NULL || frame == NULL ||
        result->state != DF_GVS_ACCESS_PROTOCOL_WAITING ||
        now_ms < result->last_now_ms || now_ms >= result->deadline_ms ||
        !df_gvs_access_result_current(result, session, local) ||
        frame->family != 0x04 || frame->opcode != 0x89 ||
        frame->payload == NULL || frame->payload_length != 1U ||
        memcmp(frame->source, result->destination, 6) != 0 ||
        memcmp(frame->destination, result->source, 6) != 0)
        return DF_ERR_INVALID;
    result->last_now_ms = now_ms;
    result->deadline_ms = 0;
    result->raw_status = frame->payload[0];
    result->physical_result_confirmed = false;
    result->state = frame->payload[0] == 0x01
        ? DF_GVS_ACCESS_PROTOCOL_COMPLETED
        : DF_GVS_ACCESS_PROTOCOL_REJECTED;
    return DF_OK;
}

int df_gvs_access_result_tick(
    struct df_gvs_access_result *result, const struct df_gvs_session *session,
    const uint8_t local[6], uint64_t now_ms) {
    if (result == NULL || session == NULL || local == NULL ||
        now_ms < result->last_now_ms)
        return DF_ERR_INVALID;
    result->last_now_ms = now_ms;
    if (result->state != DF_GVS_ACCESS_PROTOCOL_WAITING)
        return DF_OK;
    if (!df_gvs_access_result_current(result, session, local)) {
        result->state = DF_GVS_ACCESS_PROTOCOL_CANCELLED;
        result->deadline_ms = 0;
    } else if (now_ms >= result->deadline_ms) {
        result->state = DF_GVS_ACCESS_PROTOCOL_EXPIRED;
        result->deadline_ms = 0;
    }
    return DF_OK;
}

int df_gvs_access_control_init(struct df_gvs_access_control *control,
    const char *material, uint64_t now_ms, df_gvs_access_send_fn send,
    void *context) {
    size_t i;
    if (control == NULL) return DF_ERR_INVALID;
    memset(control, 0, sizeof(*control));
    df_gvs_access_result_init(&control->result, now_ms);
    control->send = send;
    control->send_context = context;
    if (material == NULL || material[0] == '\0') return DF_OK;
    if (strlen(material) != 16U ||
        strspn(material, "0123456789abcdefABCDEF") != 16U)
        return DF_ERR_INVALID;
    for (i = 0; i < 16U; i++) {
        unsigned char c = (unsigned char)material[i];
        unsigned value = c <= '9' ? c - '0' : (c | 32U) - 'a' + 10U;
        control->material[i / 2U] |= (uint8_t)(value << (i % 2U ? 0U : 4U));
    }
    control->configured = true;
    return DF_OK;
}

int df_gvs_access_control_submit(struct df_gvs_access_control *control,
    const struct df_gvs_session *session, uint64_t generation,
    const uint8_t local[6], uint64_t now_ms) {
    struct df_gvs_access_request request;
    struct df_gvs_access_result next;
    int status;
    if (control == NULL || !control->configured || control->send == NULL ||
        generation == control->attempted_generation ||
        df_gvs_access_prepare_direct(session, generation, local,
            control->material, &request) != DF_OK)
        return DF_ERR_INVALID;
    next = control->result;
    if (df_gvs_access_result_tick(&next, session, local, now_ms) != DF_OK ||
        df_gvs_access_result_begin(&next, session, generation, local,
            now_ms, 1000U) != DF_OK)
        return DF_ERR_INVALID;
    /* No transaction identifier exists in 04/89: allow one attempt per call. */
    control->attempted_generation = generation;
    status = control->send(&request, control->send_context);
    if (status != DF_OK) {
        next.state = DF_GVS_ACCESS_PROTOCOL_SEND_FAILED;
        next.deadline_ms = 0;
    }
    control->result = next;
    return status;
}

const char *df_gvs_access_state_name(enum df_gvs_access_result_state state) {
    switch (state) {
    case DF_GVS_ACCESS_PROTOCOL_EMPTY: return "idle";
    case DF_GVS_ACCESS_PROTOCOL_WAITING: return "waiting";
    case DF_GVS_ACCESS_PROTOCOL_COMPLETED: return "protocol_completed";
    case DF_GVS_ACCESS_PROTOCOL_REJECTED: return "protocol_rejected";
    case DF_GVS_ACCESS_PROTOCOL_EXPIRED: return "expired";
    case DF_GVS_ACCESS_PROTOCOL_CANCELLED: return "cancelled";
    case DF_GVS_ACCESS_PROTOCOL_SEND_FAILED: return "send_failed";
    }
    return "unknown";
}
