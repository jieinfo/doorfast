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
    const char *phase_name;
    const char *role_name;
    void *sync_table;
    void *call_table;
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
    blobmsg_add_string(&platform->response, "mode", "passive");
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
    blobmsg_close_table(&platform->response, call_table);
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

static const struct ubus_method df_runtime_ubus_methods[] = {
    UBUS_METHOD_NOARG("status", df_runtime_ubus_status_handler),
    UBUS_METHOD("answer", df_runtime_ubus_answer_handler,
                df_runtime_ubus_answer_policy),
    UBUS_METHOD("hangup", df_runtime_ubus_hangup_handler,
                df_runtime_ubus_hangup_policy),
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
