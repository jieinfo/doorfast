#include "gvs_identity.h"

#include <stdio.h>
#include <string.h>

static uint8_t df_gvs_bcd(unsigned value) {
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
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
        room < 101U || room > 6332U || room % 100U == 0U ||
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

int df_gvs_frame_is_for_identity(const struct df_gvs_frame *frame,
                                 const uint8_t identity[6]) {
    if (frame == NULL || identity == NULL) {
        return 0;
    }
    return memcmp(frame->destination, identity, 5) == 0;
}
