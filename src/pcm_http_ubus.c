#include "pcm_http_ubus.h"

#include <string.h>

#include "doorfast.h"
#include "gvs_pcm_ingress.h"
#include "runtime_id.h"

#ifdef DF_WITH_UBUS

#include <libubox/blobmsg_json.h>
#include <libubus.h>

enum root_field {
    ROOT_RUNTIME_ID,
    ROOT_CALL,
    ROOT_AUDIO_TX,
    ROOT_FIELD_COUNT
};

enum call_field {
    CALL_SESSION,
    CALL_GENERATION,
    CALL_FIELD_COUNT
};

enum audio_tx_field {
    AUDIO_TX_ACTIVE,
    AUDIO_TX_GENERATION,
    AUDIO_TX_FIELD_COUNT
};

static const struct blobmsg_policy root_policy[ROOT_FIELD_COUNT] = {
    [ROOT_RUNTIME_ID] = { .name = "runtime_id", .type = BLOBMSG_TYPE_STRING },
    [ROOT_CALL] = { .name = "call", .type = BLOBMSG_TYPE_TABLE },
    [ROOT_AUDIO_TX] = { .name = "audio_tx", .type = BLOBMSG_TYPE_TABLE }
};

static const struct blobmsg_policy call_policy[CALL_FIELD_COUNT] = {
    [CALL_SESSION] = { .name = "session", .type = BLOBMSG_TYPE_STRING },
    [CALL_GENERATION] = { .name = "generation", .type = BLOBMSG_TYPE_INT64 }
};

static const struct blobmsg_policy audio_tx_policy[AUDIO_TX_FIELD_COUNT] = {
    [AUDIO_TX_ACTIVE] = { .name = "active", .type = BLOBMSG_TYPE_BOOL },
    [AUDIO_TX_GENERATION] = {
        .name = "generation", .type = BLOBMSG_TYPE_INT64
    }
};

struct status_parse_context {
    struct df_pcm_http_status parsed;
    bool valid;
};

static void status_callback(struct ubus_request *request, int type,
                            struct blob_attr *message)
{
    struct status_parse_context *context = request->priv;
    struct blob_attr *root[ROOT_FIELD_COUNT] = {0};
    struct blob_attr *call[CALL_FIELD_COUNT] = {0};
    struct blob_attr *audio_tx[AUDIO_TX_FIELD_COUNT] = {0};
    struct df_pcm_http_status parsed = {0};
    const char *runtime_id;
    const char *session;

    (void)type;
    if (context == NULL || message == NULL)
        return;
    if (blobmsg_parse(root_policy, ROOT_FIELD_COUNT, root,
                      blob_data(message), blob_len(message)) != 0)
        return;
    if (root[ROOT_RUNTIME_ID] == NULL || root[ROOT_CALL] == NULL ||
        root[ROOT_AUDIO_TX] == NULL)
        return;
    if (blobmsg_parse(call_policy, CALL_FIELD_COUNT, call,
                      blobmsg_data(root[ROOT_CALL]),
                      blobmsg_len(root[ROOT_CALL])) != 0 ||
        blobmsg_parse(audio_tx_policy, AUDIO_TX_FIELD_COUNT, audio_tx,
                      blobmsg_data(root[ROOT_AUDIO_TX]),
                      blobmsg_len(root[ROOT_AUDIO_TX])) != 0)
        return;
    if (call[CALL_SESSION] == NULL || call[CALL_GENERATION] == NULL ||
        audio_tx[AUDIO_TX_ACTIVE] == NULL ||
        audio_tx[AUDIO_TX_GENERATION] == NULL)
        return;

    runtime_id = blobmsg_get_string(root[ROOT_RUNTIME_ID]);
    session = blobmsg_get_string(call[CALL_SESSION]);
    if (!df_runtime_id_is_valid(runtime_id) ||
        strlen(session) >= sizeof(parsed.call_state))
        return;
    parsed.generation = blobmsg_get_u64(call[CALL_GENERATION]);
    parsed.audio_tx_generation =
        blobmsg_get_u64(audio_tx[AUDIO_TX_GENERATION]);
    if (blobmsg_get_u8(audio_tx[AUDIO_TX_ACTIVE]) > 1U ||
        parsed.generation == 0U || parsed.audio_tx_generation == 0U ||
        parsed.generation != parsed.audio_tx_generation)
        return;

    memcpy(parsed.runtime_id, runtime_id, sizeof(parsed.runtime_id));
    memcpy(parsed.call_state, session, strlen(session) + 1U);
    parsed.audio_tx_active = blobmsg_get_bool(audio_tx[AUDIO_TX_ACTIVE]);
    context->parsed = parsed;
    context->valid = true;
}

#endif

int df_pcm_http_ubus_status(struct df_pcm_http_status *status, void *context)
{
#ifdef DF_WITH_UBUS
    struct status_parse_context parse_context = {0};
    struct ubus_context *ubus;
    uint32_t object_id;
    int result;

    (void)context;
    if (status == NULL)
        return DF_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    ubus = ubus_connect(NULL);
    if (ubus == NULL)
        return DF_ERR_IO;
    result = ubus_lookup_id(ubus, "doorfast", &object_id);
    if (result == 0) {
        result = ubus_invoke(ubus, object_id, "status", NULL,
                             status_callback, &parse_context, 1000);
    }
    ubus_free(ubus);
    if (result != 0 || !parse_context.valid)
        return DF_ERR_IO;
    *status = parse_context.parsed;
    return DF_OK;
#else
    (void)status;
    (void)context;
    return DF_ERR_IO;
#endif
}

int df_pcm_http_ubus_send(const char *path, uint64_t generation,
                          const int16_t *pcm, size_t sample_count,
                          void *context)
{
    (void)context;
    return df_gvs_pcm_ingress_send(path, generation, pcm, sample_count) == DF_OK
        ? DF_OK : DF_ERR_IO;
}
