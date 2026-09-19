#ifndef DOORFAST_STATION_REGISTRY_H
#define DOORFAST_STATION_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

enum df_station_route_preference {
    DF_STATION_ROUTE_DISCOVER_FIRST = 0,
    DF_STATION_ROUTE_FIXED,
};

struct df_station {
    char id[33];
    char name[65];
    uint8_t logical_address[6];
    uint32_t configured_ipv4;
    char stream_name[65];
    enum df_station_route_preference route_preference;
    bool enabled;
};

struct df_station_registry {
    struct df_station *items;
    size_t count;
    uint64_t revision;
};

int df_station_registry_parse(struct df_station_registry *, const char *);
int df_station_registry_load(struct df_station_registry *, const char *);
const struct df_station *df_station_registry_find(
    const struct df_station_registry *, const char *);
void df_station_registry_destroy(struct df_station_registry *);

#endif
