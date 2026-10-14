#ifndef DOORFAST_RUNTIME_STATION_CALL_H
#define DOORFAST_RUNTIME_STATION_CALL_H

#include "runtime_service.h"
#include "runtime_ubus.h"

bool df_runtime_station_call_active(const struct df_runtime_ubus *);
int df_runtime_station_call_admit(struct df_runtime_ubus *, const char *, uint64_t *);
int df_runtime_station_call_hangup(struct df_runtime_ubus *, const char *, uint64_t);
int df_runtime_station_call_install(struct df_runtime_call_binding *,
    const struct df_runtime_call_request *, uint64_t);
int df_runtime_station_call_step(struct df_runtime_ubus *, uint64_t);
int df_runtime_call_step(struct df_runtime_call_binding *, uint64_t,
    struct df_gvs_call_control_result *);
int df_runtime_call_hangup(struct df_runtime_call_binding *, uint64_t,
    uint8_t, uint64_t);
void df_runtime_station_call_shutdown(struct df_runtime_ubus *, uint64_t);
void df_runtime_station_call_preempt(struct df_runtime_ubus *, uint64_t);
void df_runtime_call_release_ended(struct df_runtime_call_binding *, uint64_t);

#endif
