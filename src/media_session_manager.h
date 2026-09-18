#ifndef DOORFAST_MEDIA_SESSION_MANAGER_H
#define DOORFAST_MEDIA_SESSION_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "media_session.h"

typedef int (*df_media_session_resource_start_fn)(
    struct df_media_session *, uint64_t proposed_generation, void *context);
typedef void (*df_media_session_resource_stop_fn)(
    struct df_media_session *, void *context);

struct df_media_session_resource_hooks {
    df_media_session_resource_start_fn start;
    df_media_session_resource_stop_fn stop;
    void *context;
};

struct df_media_session_manager {
    struct df_media_module_config_v3 config;
    struct df_media_module_callbacks_v3 callbacks;
    struct df_media_station_config_v3 *stations;
    struct df_media_session *sessions;
    size_t capacity;
    size_t active_count;
    uint64_t next_generation;
    struct df_media_session_resource_hooks resource_hooks;
    char *go2rtc_host;
    char *rtsp_username;
    char *credentials_path;
    bool initialized;
};

int df_media_session_manager_init(struct df_media_session_manager *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
void df_media_session_manager_destroy(struct df_media_session_manager *);
int df_media_session_manager_start(struct df_media_session_manager *,
    const char *station_id, enum df_media_session_purpose, uint64_t now_ms,
    uint64_t *generation);
int df_media_session_manager_command(struct df_media_session_manager *,
    enum df_media_module_command, const struct df_media_session_key *,
    bool active, uint64_t now_ms);
size_t df_media_session_manager_active(
    const struct df_media_session_manager *);
size_t df_media_session_manager_capacity(
    const struct df_media_session_manager *);
const struct df_media_session *df_media_session_manager_lookup(
    const struct df_media_session_manager *,
    const struct df_media_session_key *);
void df_media_session_manager_set_resource_hooks(
    struct df_media_session_manager *,
    const struct df_media_session_resource_hooks *);

#endif
