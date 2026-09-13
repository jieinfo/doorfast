#ifndef DOORFAST_GVS_ELEVATOR_QUERY_H
#define DOORFAST_GVS_ELEVATOR_QUERY_H

#include <stdbool.h>
#include <stdint.h>

#include "gvs_elevator.h"

#define DF_GVS_ELEVATOR_QUERY_INTERVAL_MS 1000U

struct df_gvs_elevator_query {
    bool enabled;
    uint8_t local[6];
    uint64_t last_now_ms;
    uint64_t next_query_ms;
    unsigned attempts;
    unsigned successful_sends;
    unsigned failed_sends;
    df_gvs_elevator_send_fn send;
    void *send_context;
};

int df_gvs_elevator_query_init(struct df_gvs_elevator_query *,
    const uint8_t local[6], bool enabled, uint64_t now_ms,
    df_gvs_elevator_send_fn, void *);
int df_gvs_elevator_query_tick(struct df_gvs_elevator_query *, uint64_t now_ms);

#endif
