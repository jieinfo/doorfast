#include "runtime_ubus.h"

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

static int df_runtime_ubus_status_handler(
    struct ubus_context *context, struct ubus_object *object,
    struct ubus_request_data *request, const char *method,
    struct blob_attr *message) {
    struct df_runtime_ubus_platform *platform =
        container_of(object, struct df_runtime_ubus_platform, object);
    struct df_gvs_runtime_sync_status status;
    const char *phase_name;
    const char *role_name;
    void *sync_table;
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
    result = ubus_send_reply(context, request, platform->response.head);
    blob_buf_free(&platform->response);
    return result == 0 ? UBUS_STATUS_OK : UBUS_STATUS_UNKNOWN_ERROR;
}

static const struct ubus_method df_runtime_ubus_methods[] = {
    UBUS_METHOD_NOARG("status", df_runtime_ubus_status_handler),
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

void df_runtime_ubus_stop(struct df_runtime_ubus *service) {
    if (service == NULL || !service->started) {
        return;
    }
#ifdef DF_WITH_UBUS
    df_runtime_ubus_platform_stop(service);
#endif
    memset(service, 0, sizeof(*service));
}
