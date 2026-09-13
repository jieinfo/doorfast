#ifndef DOORFAST_RUNTIME_UBUS_H
#define DOORFAST_RUNTIME_UBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_access.h"
#include "gvs_elevator_control.h"
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

struct df_runtime_elevator_status {
    enum df_gvs_elevator_control_state state;
    enum df_gvs_elevator_direction direction;
    uint64_t transaction_id;
    unsigned attempts;
    unsigned successful_sends;
    bool physical_result_confirmed;
    bool status_valid;
    size_t count;
    uint64_t age_ms;
    struct df_gvs_elevator_entry entries[DF_GVS_ELEVATOR_MAX_ENTRIES];
};

struct df_runtime_ubus {
    df_runtime_status_provider_fn provide_status;
    void *status_context;
    df_runtime_call_status_provider_fn provide_call_status;
    df_runtime_call_submit_fn submit_call;
    void *call_context;
    struct df_gvs_access_control *access;
    const struct df_gvs_session *access_session;
    const uint8_t *access_identity;
    struct df_gvs_elevator_control *elevator;
    const uint8_t *elevator_identity;
    uint64_t next_elevator_transaction_id;
    struct df_gvs_elevator_status observed_elevator_status;
    uint64_t elevator_status_observed_ms;
    bool elevator_status_valid;
    void *platform;
    uint64_t last_now_ms;
    uint64_t next_reconnect_ms;
    bool started;
    bool active_host;
};

int df_runtime_ubus_start(struct df_runtime_ubus *service,
                          df_runtime_status_provider_fn provide_status,
                          void *context, uint64_t now_ms);
int df_runtime_ubus_process(struct df_runtime_ubus *service,
                            uint64_t now_ms);
int df_runtime_ubus_bind_call(struct df_runtime_ubus *,
    df_runtime_call_status_provider_fn, df_runtime_call_submit_fn, void *);
void df_runtime_ubus_set_active_host(struct df_runtime_ubus *, bool);
int df_runtime_ubus_read_call_status(struct df_runtime_ubus *,
    struct df_gvs_call_control_status *);
int df_runtime_ubus_submit_call(struct df_runtime_ubus *,
    const struct df_runtime_call_request *);
void df_runtime_ubus_stop(struct df_runtime_ubus *service);

int df_runtime_ubus_bind_access(struct df_runtime_ubus *,
    struct df_gvs_access_control *, const struct df_gvs_session *,
    const uint8_t [6]);
int df_runtime_ubus_unlock(struct df_runtime_ubus *, uint64_t);
int df_runtime_ubus_bind_elevator(struct df_runtime_ubus *,
    struct df_gvs_elevator_control *, const uint8_t [6]);
int df_runtime_ubus_call_elevator(struct df_runtime_ubus *,
    enum df_gvs_elevator_direction, uint64_t *);
int df_runtime_ubus_update_elevator_status(struct df_runtime_ubus *,
    const struct df_gvs_elevator_status *, uint64_t);
int df_runtime_ubus_read_elevator_status(struct df_runtime_ubus *,
    struct df_runtime_elevator_status *);

#endif
