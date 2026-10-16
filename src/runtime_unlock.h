#ifndef DOORFAST_RUNTIME_UNLOCK_H
#define DOORFAST_RUNTIME_UNLOCK_H

#include "access_credentials.h"
#include "runtime_id.h"
#include "media_module.h"

enum df_runtime_unlock_stage {
    DF_RUNTIME_UNLOCK_IDLE, DF_RUNTIME_UNLOCK_PREPARING,
    DF_RUNTIME_UNLOCK_WAITING_REPLY, DF_RUNTIME_UNLOCK_COMPLETED,
    DF_RUNTIME_UNLOCK_REJECTED, DF_RUNTIME_UNLOCK_PREPARATION_TIMEOUT,
    DF_RUNTIME_UNLOCK_REPLY_TIMEOUT, DF_RUNTIME_UNLOCK_CANCELLED,
    DF_RUNTIME_UNLOCK_SEND_FAILED
};

struct df_runtime_unlock_operation {
    char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
    char request_id[17];
    char station_id[DF_MEDIA_MODULE_STATION_ID_MAX];
    uint64_t media_generation, access_generation;
    uint64_t preparation_generation;
    enum df_runtime_unlock_stage stage;
    enum df_station_unlock_mode effective_mode;
    enum df_access_credential_source credential_source;
    uint64_t started_ms, ended_ms, deadline_ms;
    uint64_t first_request_ms, confirmed_ms, first_media_ms, sent_ms, reply_ms;
    unsigned attempts, busy_replies;
    bool has_first_request, has_confirmed, has_first_media, has_sent, has_reply;
    bool reused_session, lightweight, automatic, cleanup_pending;
    bool raw_status_valid;
    uint8_t raw_status;
    int error, cleanup_error;
    const char *wait_reason;
};

struct df_runtime_unlock_sink {
    void *context;
    void (*string)(void *, const char *, const char *);
    void (*number)(void *, const char *, uint64_t);
    void (*boolean)(void *, const char *, bool);
};
struct df_runtime_ubus;
struct df_runtime_config;
struct df_station;
struct df_runtime_unlock_station {
    enum df_station_unlock_mode mode;
    enum df_access_credential_source credential_source;
    bool configured;
    const char *reason;
};
int df_runtime_ubus_bind_unlock_config(struct df_runtime_ubus *,
    const struct df_runtime_config *);
int df_runtime_unlock_station_metadata(const struct df_runtime_ubus *,
    const struct df_station *, struct df_runtime_unlock_station *);
const char *df_runtime_unlock_phase(enum df_runtime_unlock_stage);
const char *df_runtime_unlock_mode_name(enum df_station_unlock_mode);
const char *df_runtime_unlock_source_name(enum df_access_credential_source);
bool df_runtime_unlock_operation_valid(const struct df_runtime_unlock_operation *);
void df_runtime_unlock_emit_operation(const struct df_runtime_unlock_operation *,
    uint64_t, const struct df_runtime_unlock_sink *);
int df_runtime_unlock_send(struct df_runtime_ubus *, const char *, uint64_t);
int df_runtime_unlock_begin_auto(struct df_runtime_ubus *, const char *, uint64_t,
    uint64_t);
void df_runtime_unlock_observe_result(struct df_runtime_ubus *, uint64_t);
void df_runtime_unlock_cancel(struct df_runtime_ubus *, uint64_t);

#endif
