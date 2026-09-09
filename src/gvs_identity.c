#include "gvs_identity.h"

#include <stdio.h>
#include <string.h>

static uint8_t df_gvs_bcd(unsigned value) {
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static int df_gvs_bcd_value(uint8_t value, unsigned *decoded) {
    unsigned high = (unsigned)(value >> 4U);
    unsigned low = (unsigned)(value & 0x0FU);

    if (decoded == NULL || high > 9U || low > 9U) {
        return DF_ERR_INVALID;
    }
    *decoded = high * 10U + low;
    return DF_OK;
}

static int df_gvs_identity_fields(const uint8_t identity[6],
                                  unsigned *building_unit, unsigned *floor,
                                  unsigned *room, unsigned *machine) {
    unsigned building;
    unsigned unit;

    if (identity == NULL || identity[0] != 0x61 ||
        df_gvs_bcd_value(identity[1], &building) != DF_OK ||
        df_gvs_bcd_value(identity[2], &unit) != DF_OK ||
        df_gvs_bcd_value(identity[3], floor) != DF_OK ||
        df_gvs_bcd_value(identity[4], room) != DF_OK ||
        df_gvs_bcd_value(identity[5], machine) != DF_OK) {
        return DF_ERR_INVALID;
    }
    *building_unit = building * 10U + unit;
    if (*building_unit < 1U || *building_unit > 999U || *floor < 1U ||
        *floor > 63U || *room < 1U || *room > 32U || *machine < 1U ||
        *machine > 6U) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_gvs_identity_parse(const char *address, uint8_t identity[6]) {
    unsigned building;
    unsigned unit;
    unsigned room;
    unsigned machine;
    char trailing;

    if (address == NULL || identity == NULL ||
        sscanf(address, "IS:%u-%u-%u-%u%c", &building, &unit, &room, &machine, &trailing) != 4 ||
        building < 1U || building > 99U || unit < 1U || unit > 9U ||
        room < 101U || room > 6332U || room % 100U == 0U || room % 100U > 32U ||
        machine < 1U || machine > 4U) {
        return DF_ERR_INVALID;
    }

    identity[0] = 0x61;
    identity[1] = df_gvs_bcd(building);
    identity[2] = df_gvs_bcd(unit);
    identity[3] = df_gvs_bcd(room / 100U);
    identity[4] = df_gvs_bcd(room % 100U);
    identity[5] = df_gvs_bcd(machine);
    return DF_OK;
}

int df_gvs_identity_unicast_ip(const uint8_t identity[6],
                               char ip[DF_GVS_IPV4_TEXT_SIZE]) {
    unsigned building_unit;
    unsigned floor;
    unsigned room;
    unsigned machine;
    unsigned second;
    unsigned third;
    unsigned fourth;

    if (ip == NULL ||
        df_gvs_identity_fields(identity, &building_unit, &floor, &room,
                               &machine) != DF_OK) {
        return DF_ERR_INVALID;
    }
    second = building_unit >> 2U;
    third = ((building_unit & 3U) << 6U) | floor;
    fourth = ((machine - 1U) << 5U) | (room - 1U);
    if (snprintf(ip, DF_GVS_IPV4_TEXT_SIZE, "10.%u.%u.%u", second, third,
                 fourth) >= DF_GVS_IPV4_TEXT_SIZE) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_gvs_identity_multicast_ip(const uint8_t identity[6],
                                 char ip[DF_GVS_IPV4_TEXT_SIZE]) {
    unsigned building_unit;
    unsigned floor;
    unsigned room;
    unsigned machine;
    unsigned value;

    if (ip == NULL ||
        df_gvs_identity_fields(identity, &building_unit, &floor, &room,
                               &machine) != DF_OK) {
        return DF_ERR_INVALID;
    }
    (void)machine;
    value = (building_unit - 1U) * 2528U + 1024U +
            (floor - 1U) * 32U + room;
    if (snprintf(ip, DF_GVS_IPV4_TEXT_SIZE, "238.%u.%u.%u",
                 (value >> 16U) & 0xFFU, (value >> 8U) & 0xFFU,
                 value & 0xFFU) >= DF_GVS_IPV4_TEXT_SIZE) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_gvs_identity_indoor_peers(
    const uint8_t identity[6],
    uint8_t peers[DF_GVS_INDOOR_PEER_COUNT][6]) {
    unsigned building_unit;
    unsigned floor;
    unsigned room;
    unsigned machine;
    size_t count = 0;
    unsigned candidate;

    if (peers == NULL ||
        df_gvs_identity_fields(identity, &building_unit, &floor, &room,
                               &machine) != DF_OK ||
        machine > 4U) {
        return DF_ERR_INVALID;
    }
    (void)building_unit;
    (void)floor;
    (void)room;
    for (candidate = 1U; candidate <= 4U; candidate++) {
        if (candidate != machine) {
            memcpy(peers[count], identity, 6);
            peers[count][5] = (uint8_t)candidate;
            count++;
        }
    }
    for (candidate = 1U; candidate <= 2U; candidate++) {
        memcpy(peers[count], identity, 6);
        peers[count][0] = 0x62;
        peers[count][5] = (uint8_t)candidate;
        count++;
    }
    return count == DF_GVS_INDOOR_PEER_COUNT ? DF_OK : DF_ERR_INVALID;
}

int df_gvs_frame_is_for_identity(const struct df_gvs_frame *frame,
                                 const uint8_t identity[6]) {
    if (frame == NULL || identity == NULL) {
        return 0;
    }
    return memcmp(frame->destination, identity, 5) == 0;
}
