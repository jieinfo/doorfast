#include "gvs_station.h"

#include <string.h>

static int df_gvs_station_hex(char value, unsigned *result) {
    if (result == NULL) {
        return DF_ERR_INVALID;
    }
    if (value >= '0' && value <= '9') {
        *result = (unsigned)(value - '0');
        return DF_OK;
    }
    if (value >= 'a' && value <= 'f') {
        *result = (unsigned)(value - 'a') + 10U;
        return DF_OK;
    }
    if (value >= 'A' && value <= 'F') {
        *result = (unsigned)(value - 'A') + 10U;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

static bool df_gvs_station_bcd(uint8_t value) {
    return (value >> 4U) <= 9U && (value & 0x0fU) <= 9U;
}

static bool df_gvs_station_valid(const uint8_t address[6]) {
    return address != NULL && address[0] == 0x32U &&
        df_gvs_station_bcd(address[1]) && address[1] != 0U &&
        df_gvs_station_bcd(address[2]) && address[2] != 0U &&
        address[3] == 0U && df_gvs_station_bcd(address[4]) &&
        address[4] != 0U && address[5] == 0U;
}

int df_gvs_station_parse(const char *text, uint8_t output[6]) {
    size_t index;

    if (text == NULL || output == NULL || strlen(text) != 17U) {
        return DF_ERR_INVALID;
    }
    for (index = 0; index < 6U; index++) {
        unsigned high;
        unsigned low;
        size_t offset = index * 3U;

        if ((index < 5U && text[offset + 2U] != ':') ||
            df_gvs_station_hex(text[offset], &high) != DF_OK ||
            df_gvs_station_hex(text[offset + 1U], &low) != DF_OK) {
            return DF_ERR_INVALID;
        }
        output[index] = (uint8_t)((high << 4U) | low);
    }
    return df_gvs_station_valid(output) ? DF_OK : DF_ERR_INVALID;
}

static struct df_gvs_station_route *df_gvs_station_route_find_mutable(
    struct df_gvs_station_routes *routes, const uint8_t peer[6]) {
    size_t index;

    for (index = 0; index < DF_GVS_STATION_ROUTE_CAPACITY; index++) {
        if (routes->entries[index].valid &&
            memcmp(routes->entries[index].peer, peer, 6U) == 0) {
            return &routes->entries[index];
        }
    }
    return NULL;
}

int df_gvs_station_routes_observe(struct df_gvs_station_routes *routes,
    const uint8_t peer[6], uint32_t ipv4, uint64_t now_ms,
    bool discovery_reply) {
    struct df_gvs_station_route *entry;

    if (routes == NULL || !discovery_reply || !df_gvs_station_valid(peer) ||
        ipv4 == 0U) {
        return DF_ERR_INVALID;
    }
    entry = df_gvs_station_route_find_mutable(routes, peer);
    if (entry != NULL && now_ms < entry->discovery_ms) {
        return DF_ERR_INVALID;
    }
    if (entry == NULL) {
        entry = &routes->entries[routes->next];
        routes->next = (routes->next + 1U) % DF_GVS_STATION_ROUTE_CAPACITY;
    }
    memcpy(entry->peer, peer, sizeof(entry->peer));
    entry->ipv4 = ipv4;
    entry->discovery_ms = now_ms;
    entry->valid = true;
    return DF_OK;
}

int df_gvs_station_routes_lookup(const struct df_gvs_station_routes *routes,
    const uint8_t peer[6], uint64_t now_ms, uint64_t max_age_ms,
    uint32_t *ipv4) {
    size_t index;

    if (routes == NULL || !df_gvs_station_valid(peer) || max_age_ms == 0U ||
        ipv4 == NULL) {
        return DF_ERR_INVALID;
    }
    for (index = 0; index < DF_GVS_STATION_ROUTE_CAPACITY; index++) {
        const struct df_gvs_station_route *entry = &routes->entries[index];
        if (!entry->valid || memcmp(entry->peer, peer, 6U) != 0) {
            continue;
        }
        if (now_ms < entry->discovery_ms ||
            now_ms - entry->discovery_ms >= max_age_ms) {
            return DF_ERR_INVALID;
        }
        *ipv4 = entry->ipv4;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}
