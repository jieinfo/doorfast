#ifndef DOORFAST_RUNTIME_SERVICE_H
#define DOORFAST_RUNTIME_SERVICE_H

#include <stdint.h>

#include "gvs_call_control.h"
#include "gvs_runtime_sync.h"
#include "gvs_station_discovery.h"
#include "runtime_media_module.h"
#include "runtime_config.h"

typedef int (*df_runtime_delay_slice_fn)(unsigned delay_ms, void *context);
typedef int (*df_runtime_station_scan_emit_fn)(
    const struct df_gvs_station_scan_action *, void *context);

int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms,
                          df_runtime_delay_slice_fn run_slice,
                          void *context);
int df_runtime_station_scan_tick(struct df_gvs_station_scan *, uint64_t now_ms,
    df_runtime_station_scan_emit_fn, void *context);
int df_runtime_sync_configure(struct df_gvs_runtime_sync *,
    const struct df_runtime_config *);
int df_runtime_media_build_module_config(const struct df_runtime_config *,
    const uint8_t local[6], struct df_media_module_config_v2 *);
int df_runtime_receive_control_with_media(struct df_runtime_media_module *,
    struct df_gvs_call_control *, const uint8_t *data, size_t length,
    const uint8_t local[6], struct df_gvs_session *, struct df_gvs_deadline *,
    uint32_t source_ipv4, uint64_t now_ms,
    struct df_gvs_call_control_result *);
int df_runtime_service_run(const struct df_runtime_config *runtime);

#endif
