#ifndef DOORFAST_GVS_SYNC_STATE_H
#define DOORFAST_GVS_SYNC_STATE_H

#include <stdint.h>

#include "doorfast.h"

int df_gvs_sync_state_load(const char *path, uint16_t *version);
int df_gvs_sync_state_save(const char *path, uint16_t version);

#endif
