#ifndef DOORFAST_RUNTIME_UBUS_H
#define DOORFAST_RUNTIME_UBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_runtime_sync.h"
#include "gvs_call_control.h"

typedef int (*df_runtime_status_provider_fn)(
    struct df_gvs_runtime_sync_status *status, void *context);
typedef int (*df_runtime_call_status_provider_fn)(
    struct df_gvs_call_control_status *status, void *context);

struct df_runtime_call_request {
    enum df_gvs_call_command_type type;
    uint64_t session_generation;
    uint16_t primary_media_port;
    uint16_t secondary_media_port;
    uint8_t duration_seconds;
    uint8_t reason;
};

typedef int (*df_runtime_call_submit_fn)(
    const struct df_runtime_call_request *, uint64_t, void *);

struct df_runtime_ubus {
    df_runtime_status_provider_fn provide_status;
    void *status_context;
    df_runtime_call_status_provider_fn provide_call_status;
    df_runtime_call_submit_fn submit_call;
    void *call_context;
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
int df_runtime_ubus_bind_call(struct df_runtime_ubus *,
    df_runtime_call_status_provider_fn, df_runtime_call_submit_fn, void *);
int df_runtime_ubus_read_call_status(struct df_runtime_ubus *,
    struct df_gvs_call_control_status *);
int df_runtime_ubus_submit_call(struct df_runtime_ubus *,
    const struct df_runtime_call_request *);
void df_runtime_ubus_stop(struct df_runtime_ubus *service);

#endif
