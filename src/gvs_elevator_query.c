#include "gvs_elevator_query.h"

#include <limits.h>
#include <string.h>

int df_gvs_elevator_query_init(struct df_gvs_elevator_query *query,
    const uint8_t local[6], bool enabled, uint64_t now_ms,
    df_gvs_elevator_send_fn send, void *send_context) {
    struct df_gvs_elevator_query next = {0};
    struct df_gvs_elevator_request request;

    if (query == NULL || local == NULL || send == NULL ||
        df_gvs_elevator_prepare_query(local, &request) != DF_OK)
        return DF_ERR_INVALID;
    memcpy(next.local, local, sizeof(next.local));
    next.enabled = enabled;
    next.last_now_ms = now_ms;
    next.next_query_ms = now_ms;
    next.send = send;
    next.send_context = send_context;
    *query = next;
    return DF_OK;
}

int df_gvs_elevator_query_tick(struct df_gvs_elevator_query *query,
    uint64_t now_ms) {
    struct df_gvs_elevator_query next;
    struct df_gvs_elevator_request request;
    int status;

    if (query == NULL || query->send == NULL || now_ms < query->last_now_ms ||
        (query->enabled && now_ms > UINT64_MAX -
            DF_GVS_ELEVATOR_QUERY_INTERVAL_MS))
        return DF_ERR_INVALID;
    next = *query;
    next.last_now_ms = now_ms;
    if (!next.enabled || now_ms < next.next_query_ms) {
        *query = next;
        return DF_OK;
    }
    if (df_gvs_elevator_prepare_query(next.local, &request) != DF_OK)
        return DF_ERR_INVALID;
    if (next.attempts < UINT_MAX)
        next.attempts++;
    status = next.send(&request, next.send_context);
    if (status == DF_OK) {
        if (next.successful_sends < UINT_MAX)
            next.successful_sends++;
    } else if (next.failed_sends < UINT_MAX) {
        next.failed_sends++;
    }
    next.next_query_ms = now_ms + DF_GVS_ELEVATOR_QUERY_INTERVAL_MS;
    *query = next;
    return DF_OK;
}
