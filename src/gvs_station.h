#ifndef DOORFAST_GVS_STATION_H
#define DOORFAST_GVS_STATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_GVS_STATION_ROUTE_CAPACITY 4U

struct df_gvs_station_route {
    uint8_t peer[6];
    uint32_t ipv4;
    uint64_t discovery_ms;
    bool valid;
};

struct df_gvs_station_routes {
    struct df_gvs_station_route entries[DF_GVS_STATION_ROUTE_CAPACITY];
    size_t next;
};

int df_gvs_station_parse(const char *text, uint8_t output[6]);
int df_gvs_station_routes_observe(struct df_gvs_station_routes *,
    const uint8_t peer[6], uint32_t ipv4, uint64_t now_ms,
    bool discovery_reply);
int df_gvs_station_routes_lookup(const struct df_gvs_station_routes *,
    const uint8_t peer[6], uint64_t now_ms, uint64_t max_age_ms,
    uint32_t *ipv4);

#endif
