#ifndef DOORFAST_RUNTIME_UBUS_H
#define DOORFAST_RUNTIME_UBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_runtime_sync.h"

typedef int (*df_runtime_status_provider_fn)(
    struct df_gvs_runtime_sync_status *status, void *context);

struct df_runtime_ubus {
    df_runtime_status_provider_fn provide_status;
    void *status_context;
    void *platform;
    uint64_t last_now_ms;
    uint64_t next_reconnect_ms;
    bool started;
};

int df_runtime_ubus_start(struct df_runtime_ubus *service,
                          df_runtime_status_provider_fn provide_status,
                          void *context, uint64_t now_ms);
int df_runtime_ubus_process(struct df_runtime_ubus *service,
                            uint64_t now_ms);
void df_runtime_ubus_stop(struct df_runtime_ubus *service);

#endif
