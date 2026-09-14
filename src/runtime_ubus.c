#include "runtime_ubus.h"
#include "deployment_health.h"

#include <string.h>

#define DF_RUNTIME_UBUS_RECONNECT_MS 5000U

#ifdef DF_WITH_UBUS

#include <errno.h>
#include <poll.h>
#include <stdlib.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

struct df_runtime_ubus_platform {
    struct ubus_context context;
    struct ubus_object object;
    struct blob_buf response;
    struct df_runtime_ubus *owner;
    bool connected;
};

static void df_ubus_link_health(struct blob_buf *b, const char *key,
    const struct df_link_health *link) {
    void *table = blobmsg_open_table(b, key);
    blobmsg_add_string(b, "name", link->name);
    blobmsg_add_u8(b, "present", link->present);
    if (link->carrier_known) blobmsg_add_u8(b, "carrier", link->carrier);
    if (link->counters_known) {
        blobmsg_add_u64(b, "rx_packets", link->rx_packets);
        blobmsg_add_u64(b, "tx_packets", link->tx_packets);
        blobmsg_add_u64(b, "rx_dropped", link->rx_dropped);
        blobmsg_add_u64(b, "tx_dropped", link->tx_dropped);
        blobmsg_add_u64(b, "rx_errors", link->rx_errors);
        blobmsg_add_u64(b, "tx_errors", link->tx_errors);
    }
    blobmsg_close_table(b, table);
}
static void df_ubus_deployment_health(struct blob_buf *b) {
    struct df_deployment_health h = {0};
    (void)df_deployment_health_load(&h);
    void *table = blobmsg_open_table(b, "deployment");
    blobmsg_add_u32(b, "schema_version", 1);
    blobmsg_add_u8(b, "configured", h.configured);
    if (h.configured) {
        blobmsg_add_u8(b, "preflight_safe", h.preflight_safe);
        blobmsg_add_u8(b, "passive_only", h.passive_only);
        blobmsg_add_string(b, "observation_interface", h.observation_interface);
        df_ubus_link_health(b, "upstream", &h.upstream);
        df_ubus_link_health(b, "downstream", &h.downstream);
        df_ubus_link_health(b, "management", &h.management);
        void *recorder = blobmsg_open_table(b, "recorder");
        blobmsg_add_u8(b, "present", h.recorder_present);
        if (h.recorder_present) {
            blobmsg_add_string(b, "state", h.recorder_state);
            blobmsg_add_u64(b, "recent_bytes", h.recent_bytes);
            blobmsg_add_u64(b, "control_bytes", h.control_bytes);
            blobmsg_add_u64(b, "log_bytes", h.log_bytes);
            blobmsg_add_u64(b, "available_bytes", h.available_bytes);
            blobmsg_add_u64(b, "reserve_bytes", h.reserve_bytes);
        }
        blobmsg_close_table(b, recorder);
    }
    blobmsg_close_table(b, table);
}

static int df_runtime_ubus_status_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct df_gvs_runtime_sync_status status;
    struct df_gvs_call_control_status call_status;
    struct df_runtime_elevator_status elevator_status;
    struct df_gvs_audio_status audio_status;
    struct df_gvs_audio_tx_status audio_tx_status;
    struct df_gvs_video_status video_status;
    const char *phase_name;
    const char *role_name;
    void *sync_table;
    void *call_table;
    void *elevator_table;
    int result;

    (void)method;
    (void)message;
    memset(&status, 0, sizeof(status));
    if (platform->owner->provide_status(
            &status, platform->owner->status_context) != DF_OK) {
        return UBUS_STATUS_UNKNOWN_ERROR;
    }
    phase_name = df_gvs_runtime_sync_phase_name(status.phase);
    role_name = df_gvs_runtime_sync_role_name(status.role);
    if (phase_name == NULL || role_name == NULL) {
        return UBUS_STATUS_UNKNOWN_ERROR;
    }

    blob_buf_init(&platform->response, 0);
    blobmsg_add_u8(&platform->response, "running", 1);
    blobmsg_add_string(&platform->response, "mode",
        platform->owner->active_host ? "active_host" : "passive");
    sync_table = blobmsg_open_table(&platform->response, "sync");
    blobmsg_add_string(&platform->response, "phase", phase_name);
    blobmsg_add_string(&platform->response, "role", role_name);
    blobmsg_add_u32(&platform->response, "version", status.sync_version);
    blobmsg_add_u32(&platform->response, "periodic_misses",
                    status.periodic_misses);
    blobmsg_add_u32(&platform->response, "online_peers",
                    (uint32_t)status.online_peers);
    blobmsg_add_u32(&platform->response, "registered_adapters",
                    (uint32_t)status.registered_adapters);
    blobmsg_add_u32(&platform->response, "enabled_adapters",
                    (uint32_t)status.enabled_adapters);
    blobmsg_add_u32(&platform->response, "last_opcode",
                    status.last_receive.opcode);
    blobmsg_add_u8(&platform->response, "last_handled",
                   status.last_receive.handled);
    blobmsg_add_u8(&platform->response, "last_accepted",
                   status.last_receive.accepted);
    blobmsg_add_u8(&platform->response, "last_rejected",
                   status.last_receive.rejected);
    blobmsg_add_u8(&platform->response, "resend_local",
                   status.last_receive.resend_local);
    blobmsg_close_table(&platform->response, sync_table);
    if (df_runtime_ubus_read_call_status(
            platform->owner, &call_status) != DF_OK ||
        df_gvs_session_state_name(call_status.session_state) == NULL ||
        df_gvs_call_command_type_name(call_status.command_type) == NULL ||
        df_gvs_call_dispatch_state_name(call_status.dispatch_state) == NULL ||
        df_gvs_call_dispatch_state_name(call_status.handshake_dispatch) == NULL ||
        df_gvs_call_ack_state_name(call_status.acknowledgement_state) == NULL) {
        blob_buf_free(&platform->response);
        return UBUS_STATUS_UNKNOWN_ERROR;
    }
    call_table = blobmsg_open_table(&platform->response, "call");
    blobmsg_add_string(&platform->response, "session",
        df_gvs_session_state_name(call_status.session_state));
    blobmsg_add_u64(&platform->response, "generation",
                    call_status.session_generation);
    blobmsg_add_string(&platform->response, "command",
        df_gvs_call_command_type_name(call_status.command_type));
    blobmsg_add_string(&platform->response, "dispatch",
        df_gvs_call_dispatch_state_name(call_status.dispatch_state));
    blobmsg_add_string(&platform->response, "confirmation",
        df_gvs_call_ack_state_name(call_status.acknowledgement_state));
    blobmsg_add_u32(&platform->response, "attempts", call_status.attempts);
    blobmsg_add_string(&platform->response, "handshake_mode", "simulated");
    blobmsg_add_u8(&platform->response, "handshake_active", call_status.handshake_active);
    blobmsg_add_u32(&platform->response, "handshake_missed", call_status.handshake_missed);
    blobmsg_add_u64(&platform->response, "handshake_next_ms", call_status.handshake_next_ms);
    blobmsg_add_u64(&platform->response, "handshake_dropped", call_status.handshake_dropped);
    blobmsg_add_string(&platform->response, "handshake_dispatch",
        df_gvs_call_dispatch_state_name(call_status.handshake_dispatch));
    blobmsg_close_table(&platform->response, call_table);
    if (df_runtime_ubus_read_audio_status(platform->owner, &audio_status) == DF_OK) {
        void *audio_table = blobmsg_open_table(&platform->response, "audio");
        blobmsg_add_u8(&platform->response, "ready", audio_status.ready);
        blobmsg_add_u64(&platform->response, "generation", audio_status.generation);
        blobmsg_add_u64(&platform->response, "packets", audio_status.packet_count);
        blobmsg_add_u64(&platform->response, "bytes", audio_status.byte_count);
        blobmsg_add_u64(&platform->response, "sequence_gaps", audio_status.sequence_gaps);
        blobmsg_add_u64(&platform->response, "missing_packets", audio_status.missing_packets);
        blobmsg_add_u64(&platform->response, "duplicate_packets", audio_status.duplicate_packets);
        blobmsg_add_u64(&platform->response, "late_packets", audio_status.late_packets);
        blobmsg_add_u64(&platform->response, "buffered_bytes", audio_status.buffered_bytes);
        blobmsg_add_u64(&platform->response, "last_timestamp_ms", audio_status.last_timestamp_ms);
        blobmsg_close_table(&platform->response, audio_table);
    }
    if (df_runtime_ubus_read_audio_tx_status(
            platform->owner, &audio_tx_status) == DF_OK) {
        void *audio_tx_table = blobmsg_open_table(
            &platform->response, "audio_tx");
        blobmsg_add_u8(&platform->response, "active", audio_tx_status.active);
        blobmsg_add_u64(&platform->response, "generation",
                        audio_tx_status.generation);
        blobmsg_add_u64(&platform->response, "packets_sent",
                        audio_tx_status.packets_sent);
        blobmsg_add_u64(&platform->response, "packets_failed",
                        audio_tx_status.packets_failed);
        blobmsg_add_u32(&platform->response, "next_sequence",
                        audio_tx_status.next_sequence);
        blobmsg_add_u64(&platform->response, "next_send_ms",
                        audio_tx_status.next_send_ms);
        blobmsg_close_table(&platform->response, audio_tx_table);
    }
    if (df_runtime_ubus_read_video_status(
            platform->owner, &video_status) == DF_OK) {
        void *video_table = blobmsg_open_table(&platform->response, "video");
        blobmsg_add_u8(&platform->response, "ready", video_status.ready);
        blobmsg_add_u64(&platform->response, "generation",
                        video_status.generation);
        blobmsg_add_u32(&platform->response, "frame_no",
                        video_status.frame_no);
        blobmsg_add_u64(&platform->response, "bytes", video_status.bytes);
        blobmsg_add_u64(&platform->response, "timestamp_ms",
                        video_status.timestamp_ms);
        blobmsg_close_table(&platform->response, video_table);
    }
    if (df_runtime_ubus_read_elevator_status(
            platform->owner, &elevator_status) != DF_OK) {
        blob_buf_free(&platform->response);
        return UBUS_STATUS_UNKNOWN_ERROR;
    }
    elevator_table = blobmsg_open_table(&platform->response, "elevator");
    blobmsg_add_string(&platform->response, "state",
        df_gvs_elevator_control_state_name(elevator_status.state));
    blobmsg_add_string(&platform->response, "direction",
        elevator_status.direction == DF_GVS_ELEVATOR_UP ? "up" : "down");
    blobmsg_add_u64(&platform->response, "transaction_id",
                    elevator_status.transaction_id);
    blobmsg_add_u32(&platform->response, "attempts", elevator_status.attempts);
    blobmsg_add_u32(&platform->response, "successful_sends",
                    elevator_status.successful_sends);
    blobmsg_add_u8(&platform->response, "physical_result_confirmed",
                   elevator_status.physical_result_confirmed);
    blobmsg_add_u8(&platform->response, "status_valid",
                   elevator_status.status_valid);
    blobmsg_add_u64(&platform->response, "status_age_ms",
                    elevator_status.age_ms);
    if (elevator_status.status_valid) {
        void *entries = blobmsg_open_array(&platform->response, "entries");
        size_t index;
        for (index = 0; index < elevator_status.count; index++) {
            void *entry = blobmsg_open_table(&platform->response, NULL);
            blobmsg_add_u32(&platform->response, "floor",
                (uint32_t)(int32_t)elevator_status.entries[index].floor);
            blobmsg_add_u8(&platform->response, "raw_floor",
                (uint8_t)elevator_status.entries[index].raw_floor);
            blobmsg_add_u8(&platform->response, "raw_state",
                elevator_status.entries[index].raw_state);
            blobmsg_add_string(&platform->response, "motion",
                df_gvs_elevator_motion_name(
                    elevator_status.entries[index].motion));
            blobmsg_close_table(&platform->response, entry);
        }
        blobmsg_close_array(&platform->response, entries);
    }
    blobmsg_close_table(&platform->response, elevator_table);
    if (platform->owner->access != NULL) {
        const struct df_gvs_access_control *access = platform->owner->access;
        void *table = blobmsg_open_table(&platform->response, "access");
        blobmsg_add_u8(&platform->response, "configured", access->configured);
        blobmsg_add_string(&platform->response, "state",
            df_gvs_access_state_name(access->result.state));
        blobmsg_add_u64(&platform->response, "generation",
            access->result.session_generation);
        if (access->result.state == DF_GVS_ACCESS_PROTOCOL_COMPLETED ||
            access->result.state == DF_GVS_ACCESS_PROTOCOL_REJECTED)
            blobmsg_add_u32(&platform->response, "raw_status", access->result.raw_status);
        blobmsg_add_u8(&platform->response, "physical_result_confirmed", 0);
        blobmsg_close_table(&platform->response, table);
    }
    df_ubus_deployment_health(&platform->response);
    result = ubus_send_reply(context, request, platform->response.head);
    blob_buf_free(&platform->response);
    return result == 0 ? UBUS_STATUS_OK : UBUS_STATUS_UNKNOWN_ERROR;
}

enum {
    DF_UBUS_ANSWER_GENERATION,
    DF_UBUS_ANSWER_PRIMARY_PORT,
    DF_UBUS_ANSWER_SECONDARY_PORT,
    DF_UBUS_ANSWER_DURATION,
    __DF_UBUS_ANSWER_MAX,
};

static const struct blobmsg_policy df_runtime_ubus_answer_policy[] = {
    [DF_UBUS_ANSWER_GENERATION] = {
        .name = "generation", .type = BLOBMSG_TYPE_UNSPEC},
    [DF_UBUS_ANSWER_PRIMARY_PORT] = {
        .name = "primary_media_port", .type = BLOBMSG_TYPE_INT32},
    [DF_UBUS_ANSWER_SECONDARY_PORT] = {
        .name = "secondary_media_port", .type = BLOBMSG_TYPE_INT32},
    [DF_UBUS_ANSWER_DURATION] = {
        .name = "duration_seconds", .type = BLOBMSG_TYPE_INT32},
};

enum {
    DF_UBUS_HANGUP_GENERATION,
    DF_UBUS_HANGUP_REASON,
    __DF_UBUS_HANGUP_MAX,
};

static const struct blobmsg_policy df_runtime_ubus_hangup_policy[] = {
    [DF_UBUS_HANGUP_GENERATION] = {
        .name = "generation", .type = BLOBMSG_TYPE_UNSPEC},
    [DF_UBUS_HANGUP_REASON] = {
        .name = "reason", .type = BLOBMSG_TYPE_INT32},
};

static bool df_runtime_ubus_get_generation(
    struct blob_attr *field, uint64_t *generation) {
    if (field == NULL || generation == NULL) {
        return false;
    }
    if (blobmsg_type(field) == BLOBMSG_TYPE_INT32) {
        *generation = blobmsg_get_u32(field);
        return true;
    }
    if (blobmsg_type(field) == BLOBMSG_TYPE_INT64) {
        *generation = blobmsg_get_u64(field);
        return true;
    }
    return false;
}

static int df_runtime_ubus_submit_reply(
    struct ubus_context *context, struct ubus_request_data *request,
    struct df_runtime_ubus_platform *platform,
    const struct df_runtime_call_request *call) {
    int status = df_runtime_ubus_submit_call(platform->owner, call);
    int result;

    if (status != DF_OK) {
        return status == DF_ERR_INVALID ? UBUS_STATUS_INVALID_ARGUMENT
                                        : UBUS_STATUS_UNKNOWN_ERROR;
    }
    blob_buf_init(&platform->response, 0);
    blobmsg_add_u8(&platform->response, "queued", 1);
    blobmsg_add_u64(&platform->response, "generation",
                    call->session_generation);
    result = ubus_send_reply(context, request, platform->response.head);
    blob_buf_free(&platform->response);
    return result == 0 ? UBUS_STATUS_OK : UBUS_STATUS_UNKNOWN_ERROR;
}

static int df_runtime_ubus_answer_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct blob_attr *fields[__DF_UBUS_ANSWER_MAX] = {0};
    struct df_runtime_call_request call = {
        .type = DF_GVS_CALL_COMMAND_ANSWER,
    };
    uint32_t primary;
    uint32_t secondary;
    uint32_t duration;

    (void)method;
    if (message == NULL) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    blobmsg_parse(df_runtime_ubus_answer_policy, __DF_UBUS_ANSWER_MAX,
                  fields, blob_data(message), blob_len(message));
    if (fields[DF_UBUS_ANSWER_GENERATION] == NULL ||
        fields[DF_UBUS_ANSWER_PRIMARY_PORT] == NULL ||
        fields[DF_UBUS_ANSWER_SECONDARY_PORT] == NULL ||
        fields[DF_UBUS_ANSWER_DURATION] == NULL) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    primary = blobmsg_get_u32(fields[DF_UBUS_ANSWER_PRIMARY_PORT]);
    secondary = blobmsg_get_u32(fields[DF_UBUS_ANSWER_SECONDARY_PORT]);
    duration = blobmsg_get_u32(fields[DF_UBUS_ANSWER_DURATION]);
    if (primary > UINT16_MAX || secondary > UINT16_MAX ||
        duration > UINT8_MAX) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    if (!df_runtime_ubus_get_generation(
            fields[DF_UBUS_ANSWER_GENERATION], &call.session_generation)) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    call.primary_media_port = (uint16_t)primary;
    call.secondary_media_port = (uint16_t)secondary;
    call.duration_seconds = (uint8_t)duration;
    return df_runtime_ubus_submit_reply(context, request, platform, &call);
}

static int df_runtime_ubus_hangup_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct blob_attr *fields[__DF_UBUS_HANGUP_MAX] = {0};
    struct df_runtime_call_request call = {
        .type = DF_GVS_CALL_COMMAND_HANGUP,
    };
    uint32_t reason;

    (void)method;
    if (message == NULL) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    blobmsg_parse(df_runtime_ubus_hangup_policy, __DF_UBUS_HANGUP_MAX,
                  fields, blob_data(message), blob_len(message));
    if (fields[DF_UBUS_HANGUP_GENERATION] == NULL ||
        fields[DF_UBUS_HANGUP_REASON] == NULL) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    reason = blobmsg_get_u32(fields[DF_UBUS_HANGUP_REASON]);
    if (reason > UINT8_MAX) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    if (!df_runtime_ubus_get_generation(
            fields[DF_UBUS_HANGUP_GENERATION], &call.session_generation)) {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }
    call.reason = (uint8_t)reason;
    return df_runtime_ubus_submit_reply(context, request, platform, &call);
}

static const struct blobmsg_policy df_runtime_ubus_unlock_policy[] = {
    {.name = "generation", .type = BLOBMSG_TYPE_UNSPEC},
};

static const struct blobmsg_policy df_runtime_ubus_elevator_policy[] = {
    {.name = "direction", .type = BLOBMSG_TYPE_STRING},
};

static int df_runtime_ubus_unlock_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct blob_attr *fields[1] = {0};
    uint64_t generation;
    int status;
    (void)method;
    if (message == NULL) return UBUS_STATUS_INVALID_ARGUMENT;
    blobmsg_parse(df_runtime_ubus_unlock_policy, 1, fields,
        blob_data(message), blob_len(message));
    if (!df_runtime_ubus_get_generation(fields[0], &generation))
        return UBUS_STATUS_INVALID_ARGUMENT;
    status = df_runtime_ubus_unlock(platform->owner, generation);
    if (status != DF_OK)
        return status == DF_ERR_INVALID ? UBUS_STATUS_INVALID_ARGUMENT :
                                         UBUS_STATUS_UNKNOWN_ERROR;
    blob_buf_init(&platform->response, 0);
    blobmsg_add_u8(&platform->response, "submitted", 1);
    blobmsg_add_u64(&platform->response, "generation", generation);
    status = ubus_send_reply(context, request, platform->response.head);
    blob_buf_free(&platform->response);
    return status == 0 ? UBUS_STATUS_OK : UBUS_STATUS_UNKNOWN_ERROR;
}

static int df_runtime_ubus_elevator_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct blob_attr *fields[1] = {0};
    const char *direction;
    enum df_gvs_elevator_direction value;
    uint64_t transaction_id = 0;
    int status;

    (void)method;
    if (message == NULL)
        return UBUS_STATUS_INVALID_ARGUMENT;
    blobmsg_parse(df_runtime_ubus_elevator_policy, 1, fields,
        blob_data(message), blob_len(message));
    if (fields[0] == NULL)
        return UBUS_STATUS_INVALID_ARGUMENT;
    direction = blobmsg_get_string(fields[0]);
    if (strcmp(direction, "up") == 0)
        value = DF_GVS_ELEVATOR_UP;
    else if (strcmp(direction, "down") == 0)
        value = DF_GVS_ELEVATOR_DOWN;
    else
        return UBUS_STATUS_INVALID_ARGUMENT;
    status = df_runtime_ubus_call_elevator(
        platform->owner, value, &transaction_id);
    if (status != DF_OK)
        return status == DF_ERR_INVALID ? UBUS_STATUS_INVALID_ARGUMENT :
                                         UBUS_STATUS_UNKNOWN_ERROR;
    blob_buf_init(&platform->response, 0);
    blobmsg_add_u8(&platform->response, "submitted", 1);
    blobmsg_add_u64(&platform->response, "transaction_id", transaction_id);
    blobmsg_add_string(&platform->response, "direction", direction);
    status = ubus_send_reply(context, request, platform->response.head);
    blob_buf_free(&platform->response);
    return status == 0 ? UBUS_STATUS_OK : UBUS_STATUS_UNKNOWN_ERROR;
}

static const struct ubus_method df_runtime_ubus_methods[] = {
    UBUS_METHOD("unlock", df_runtime_ubus_unlock_handler,
                df_runtime_ubus_unlock_policy),
    UBUS_METHOD_NOARG("status", df_runtime_ubus_status_handler),
    UBUS_METHOD("answer", df_runtime_ubus_answer_handler,
                df_runtime_ubus_answer_policy),
    UBUS_METHOD("hangup", df_runtime_ubus_hangup_handler,
                df_runtime_ubus_hangup_policy),
    UBUS_METHOD("call_elevator", df_runtime_ubus_elevator_handler,
                df_runtime_ubus_elevator_policy),
};

static struct ubus_object_type df_runtime_ubus_object_type =
    UBUS_OBJECT_TYPE("doorfast", df_runtime_ubus_methods);

static void df_runtime_ubus_disconnect(
    struct df_runtime_ubus_platform *platform) {
    struct df_runtime_ubus *owner = platform->owner;

    platform->context.connection_lost = NULL;
    ubus_shutdown(&platform->context);
    memset(&platform->context, 0, sizeof(platform->context));
    platform->object.id = 0;
    platform->connected = false;
    owner->next_reconnect_ms =
        owner->last_now_ms + DF_RUNTIME_UBUS_RECONNECT_MS;
}

static void df_runtime_ubus_connection_lost(struct ubus_context *context) {
    struct df_runtime_ubus_platform *platform = container_of(
        context, struct df_runtime_ubus_platform, context);

    df_runtime_ubus_disconnect(platform);
}

static int df_runtime_ubus_connect(struct df_runtime_ubus *service) {
    struct df_runtime_ubus_platform *platform = service->platform;

    memset(&platform->context, 0, sizeof(platform->context));
    if (ubus_connect_ctx(&platform->context, NULL) != 0) {
        memset(&platform->context, 0, sizeof(platform->context));
        service->next_reconnect_ms =
            service->last_now_ms + DF_RUNTIME_UBUS_RECONNECT_MS;
        return DF_ERR_IO;
    }
    platform->context.connection_lost = df_runtime_ubus_connection_lost;
    memset(&platform->object, 0, sizeof(platform->object));
    platform->object.name = "doorfast";
    platform->object.type = &df_runtime_ubus_object_type;
    platform->object.methods = df_runtime_ubus_methods;
    platform->object.n_methods = ARRAY_SIZE(df_runtime_ubus_methods);
    if (ubus_add_object(&platform->context, &platform->object) != 0) {
        df_runtime_ubus_disconnect(platform);
        return DF_ERR_IO;
    }
    platform->connected = true;
    service->next_reconnect_ms = 0;
    return DF_OK;
}

static int df_runtime_ubus_platform_start(struct df_runtime_ubus *service) {
    struct df_runtime_ubus_platform *platform = calloc(1, sizeof(*platform));

    if (platform == NULL) {
        return DF_ERR_IO;
    }
    platform->owner = service;
    service->platform = platform;
    if (df_runtime_ubus_connect(service) != DF_OK) {
        return DF_OK;
    }
    return DF_OK;
}

static int df_runtime_ubus_platform_process(struct df_runtime_ubus *service) {
    struct df_runtime_ubus_platform *platform = service->platform;
    struct pollfd descriptor;
    int result;

    if (platform == NULL) {
        return DF_ERR_IO;
    }
    if (!platform->connected) {
        if (service->last_now_ms >= service->next_reconnect_ms) {
            (void)df_runtime_ubus_connect(service);
        }
        return DF_OK;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = platform->context.sock.fd;
    descriptor.events = POLLIN;
    result = poll(&descriptor, 1, 0);
    if (result < 0) {
        if (errno == EINTR) {
            return DF_OK;
        }
        df_runtime_ubus_disconnect(platform);
        return DF_OK;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        df_runtime_ubus_disconnect(platform);
        return DF_OK;
    }
    if ((descriptor.revents & POLLIN) != 0) {
        ubus_handle_event(&platform->context);
        if (platform->connected &&
            (platform->context.sock.eof || platform->context.sock.error)) {
            df_runtime_ubus_disconnect(platform);
        }
    }
    return DF_OK;
}

static void df_runtime_ubus_platform_stop(struct df_runtime_ubus *service) {
    struct df_runtime_ubus_platform *platform = service->platform;

    if (platform == NULL) {
        return;
    }
    if (platform->connected) {
        (void)ubus_remove_object(&platform->context, &platform->object);
        platform->context.connection_lost = NULL;
        ubus_shutdown(&platform->context);
    }
    free(platform);
}

#endif

int df_runtime_ubus_start(struct df_runtime_ubus *service,
                          df_runtime_status_provider_fn provide_status,
                          void *context, uint64_t now_ms) {
    if (service == NULL || provide_status == NULL || service->started) {
        return DF_ERR_INVALID;
    }
    memset(service, 0, sizeof(*service));
    service->provide_status = provide_status;
    service->status_context = context;
    service->last_now_ms = now_ms;
    service->started = true;
#ifdef DF_WITH_UBUS
    if (df_runtime_ubus_platform_start(service) != DF_OK) {
        memset(service, 0, sizeof(*service));
        return DF_ERR_IO;
    }
#endif
    return DF_OK;
}

void df_runtime_ubus_set_active_host(struct df_runtime_ubus *service,
    bool active_host) {
    if (service != NULL) service->active_host = active_host;
}

int df_runtime_ubus_process(struct df_runtime_ubus *service,
                            uint64_t now_ms) {
    if (service == NULL || !service->started || now_ms < service->last_now_ms) {
        return DF_ERR_INVALID;
    }
    service->last_now_ms = now_ms;
#ifdef DF_WITH_UBUS
    return df_runtime_ubus_platform_process(service);
#else
    return DF_OK;
#endif
}

int df_runtime_ubus_bind_audio(struct df_runtime_ubus *service,
    struct df_gvs_audio_buffer *audio) {
    if (service == NULL || !service->started || audio == NULL || service->audio != NULL) return DF_ERR_INVALID;
    service->audio = audio;
    return DF_OK;
}

int df_runtime_ubus_read_audio_status(struct df_runtime_ubus *service,
    struct df_gvs_audio_status *status) {
    if (service == NULL || !service->started || service->audio == NULL || status == NULL) return DF_ERR_INVALID;
    return df_gvs_audio_buffer_status(service->audio, status);
}

int df_runtime_ubus_bind_audio_tx(struct df_runtime_ubus *service,
                                  struct df_gvs_audio_tx *audio_tx) {
    if (service == NULL || !service->started || audio_tx == NULL ||
        service->audio_tx != NULL)
        return DF_ERR_INVALID;
    service->audio_tx = audio_tx;
    return DF_OK;
}

int df_runtime_ubus_read_audio_tx_status(
    struct df_runtime_ubus *service, struct df_gvs_audio_tx_status *status) {
    if (service == NULL || !service->started || service->audio_tx == NULL ||
        status == NULL)
        return DF_ERR_INVALID;
    return df_gvs_audio_tx_read_status(service->audio_tx, status);
}

int df_runtime_ubus_bind_video(struct df_runtime_ubus *service,
    struct df_gvs_video_frame_cache *video) {
    if (service == NULL || !service->started || video == NULL ||
        service->video != NULL)
        return DF_ERR_INVALID;
    service->video = video;
    return DF_OK;
}

int df_runtime_ubus_read_video_status(
    struct df_runtime_ubus *service, struct df_gvs_video_status *status) {
    if (service == NULL || !service->started || service->video == NULL ||
        status == NULL)
        return DF_ERR_INVALID;
    return df_gvs_video_frame_cache_status(service->video, status);
}

int df_runtime_ubus_bind_call(
    struct df_runtime_ubus *service,
    df_runtime_call_status_provider_fn provide_call_status,
    df_runtime_call_submit_fn submit_call, void *context) {
    if (service == NULL || !service->started || provide_call_status == NULL ||
        submit_call == NULL || service->provide_call_status != NULL ||
        service->submit_call != NULL) {
        return DF_ERR_INVALID;
    }
    service->provide_call_status = provide_call_status;
    service->submit_call = submit_call;
    service->call_context = context;
    return DF_OK;
}

int df_runtime_ubus_read_call_status(
    struct df_runtime_ubus *service,
    struct df_gvs_call_control_status *status) {
    if (service == NULL || !service->started || status == NULL ||
        service->provide_call_status == NULL) {
        return DF_ERR_INVALID;
    }
    return service->provide_call_status(status, service->call_context);
}

int df_runtime_ubus_submit_call(
    struct df_runtime_ubus *service,
    const struct df_runtime_call_request *request) {
    bool answer;
    bool hangup;

    if (service == NULL || !service->started || request == NULL ||
        service->submit_call == NULL || request->session_generation == 0U) {
        return DF_ERR_INVALID;
    }
    answer = request->type == DF_GVS_CALL_COMMAND_ANSWER &&
        request->primary_media_port != 0U &&
        request->secondary_media_port != 0U &&
        request->duration_seconds != 0U && request->reason == 0U;
    hangup = request->type == DF_GVS_CALL_COMMAND_HANGUP &&
        request->primary_media_port == 0U &&
        request->secondary_media_port == 0U &&
        request->duration_seconds == 0U;
    if (!answer && !hangup) {
        return DF_ERR_INVALID;
    }
    return service->submit_call(
        request, service->last_now_ms, service->call_context);
}

void df_runtime_ubus_stop(struct df_runtime_ubus *service) {
    if (service == NULL || !service->started) {
        return;
    }
#ifdef DF_WITH_UBUS
    df_runtime_ubus_platform_stop(service);
#endif
    memset(service, 0, sizeof(*service));
}

int df_runtime_ubus_bind_access(struct df_runtime_ubus *service,
    struct df_gvs_access_control *access, const struct df_gvs_session *session,
    const uint8_t identity[6]) {
    if (service == NULL || !service->started || access == NULL ||
        session == NULL || identity == NULL || service->access != NULL)
        return DF_ERR_INVALID;
    service->access = access;
    service->access_session = session;
    service->access_identity = identity;
    return DF_OK;
}

int df_runtime_ubus_unlock(struct df_runtime_ubus *service, uint64_t generation) {
    if (service == NULL || !service->started || !service->active_host ||
        service->access == NULL || generation == 0U)
        return DF_ERR_INVALID;
    return df_gvs_access_control_submit(service->access, service->access_session,
        generation, service->access_identity, service->last_now_ms);
}

int df_runtime_ubus_bind_elevator(struct df_runtime_ubus *service,
    struct df_gvs_elevator_control *elevator, const uint8_t identity[6]) {
    struct df_gvs_elevator_request validation;

    if (service == NULL || !service->started || elevator == NULL ||
        identity == NULL || service->elevator != NULL ||
        df_gvs_elevator_prepare_query(identity, &validation) != DF_OK)
        return DF_ERR_INVALID;
    service->elevator = elevator;
    service->elevator_identity = identity;
    return DF_OK;
}

int df_runtime_ubus_call_elevator(struct df_runtime_ubus *service,
    enum df_gvs_elevator_direction direction, uint64_t *transaction_id) {
    uint64_t next_id;

    if (service == NULL || !service->started || !service->active_host ||
        service->elevator == NULL || service->elevator_identity == NULL ||
        transaction_id == NULL ||
        (direction != DF_GVS_ELEVATOR_DOWN &&
         direction != DF_GVS_ELEVATOR_UP) ||
        service->next_elevator_transaction_id == UINT64_MAX)
        return DF_ERR_INVALID;
    next_id = service->next_elevator_transaction_id + 1U;
    if (df_gvs_elevator_control_submit(service->elevator,
            service->elevator_identity, direction, next_id,
            service->last_now_ms) != DF_OK)
        return DF_ERR_INVALID;
    service->next_elevator_transaction_id = next_id;
    *transaction_id = next_id;
    return DF_OK;
}

int df_runtime_ubus_update_elevator_status(struct df_runtime_ubus *service,
    const struct df_gvs_elevator_status *status, uint64_t now_ms) {
    if (service == NULL || !service->started || status == NULL ||
        !status->valid || status->count > DF_GVS_ELEVATOR_MAX_ENTRIES ||
        now_ms < service->last_now_ms)
        return DF_ERR_INVALID;
    service->observed_elevator_status = *status;
    service->elevator_status_observed_ms = now_ms;
    service->elevator_status_valid = true;
    return DF_OK;
}

int df_runtime_ubus_read_elevator_status(struct df_runtime_ubus *service,
    struct df_runtime_elevator_status *status) {
    struct df_runtime_elevator_status next = {0};

    if (service == NULL || !service->started || service->elevator == NULL ||
        status == NULL)
        return DF_ERR_INVALID;
    next.state = service->elevator->state;
    next.transaction_id = service->elevator->transaction_id;
    next.attempts = service->elevator->attempts;
    next.successful_sends = service->elevator->successful_sends;
    next.physical_result_confirmed =
        service->elevator->physical_result_confirmed;
    if (service->elevator->request.valid)
        next.direction = (enum df_gvs_elevator_direction)
            service->elevator->request.payload[0];
    if (service->elevator_status_valid) {
        next.status_valid = true;
        next.count = service->observed_elevator_status.count;
        memcpy(next.entries, service->observed_elevator_status.entries,
               sizeof(next.entries));
        next.age_ms = service->last_now_ms -
            service->elevator_status_observed_ms;
    }
    *status = next;
    return DF_OK;
}
