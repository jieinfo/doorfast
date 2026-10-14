/* A real loadable old v3 module deliberately lacking the outbound extension.
 * Its CALL would succeed unconditionally if the runtime ever invoked it. */
#include "media_module.h"
#include "doorfast.h"
#include <string.h>

unsigned df_test_legacy_call_count;
static void *create(const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    (void)config; (void)callbacks;
    return &df_test_legacy_call_count;
}
static void destroy(void *instance) { (void)instance; }
static int start(void *instance, const char *station,
    enum df_media_session_purpose purpose, uint64_t generation, uint64_t now) {
    (void)instance; (void)station; (void)generation; (void)now;
    if (purpose == DF_MEDIA_SESSION_CALL) df_test_legacy_call_count++;
    return DF_OK;
}
static int command(void *instance, enum df_media_module_command command_id,
    const struct df_media_session_key *key, bool active, uint64_t now) {
    (void)instance; (void)command_id; (void)key; (void)active; (void)now;
    return DF_OK;
}
static int receive_control(void *instance, const struct df_gvs_frame *frame,
    uint32_t ipv4, uint64_t now) {
    (void)instance; (void)frame; (void)ipv4; (void)now;
    return DF_OK;
}
static int push_jpeg(void *instance, const uint8_t source[6],
    const uint8_t destination[6], uint32_t ipv4, const uint8_t *data,
    size_t length, uint16_t width, uint16_t height, uint64_t now) {
    (void)instance; (void)source; (void)destination; (void)ipv4; (void)data;
    (void)length; (void)width; (void)height; (void)now;
    return DF_OK;
}
static int push_video(void *instance, const struct df_gvs_video_packet *packet,
    uint32_t ipv4, uint64_t now) {
    (void)instance; (void)packet; (void)ipv4; (void)now;
    return DF_OK;
}
static int tick(void *instance, uint64_t now) {
    (void)instance; (void)now;
    return DF_OK;
}
static int status(const void *instance, struct df_media_module_status_v3 *output) {
    (void)instance;
    output->required_session_count = 1U;
    if (output->sessions != NULL && output->session_count > 0U) {
        memset(output->sessions, 0, sizeof(*output->sessions));
        strcpy(output->sessions[0].station_id, "gate_main");
        output->sessions[0].generation = 1U;
        output->sessions[0].active = true;
        output->session_count = 1U;
    } else output->session_count = 0U;
    return DF_OK;
}
const struct df_media_module_api_v3 df_media_module_api_v3 = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_api_v3),
    .create = create, .destroy = destroy, .start = start, .command = command,
    .receive_control = receive_control, .push_jpeg = push_jpeg,
    .push_video = push_video, .tick = tick, .status = status,
};
