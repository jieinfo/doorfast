#include "media_session_manager.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "doorfast.h"

static int df_media_session_resource_start_default(
    struct df_media_session *session, uint64_t proposed_generation,
    void *context) {
    (void)session;
    (void)proposed_generation;
    (void)context;
    return DF_OK;
}

static void df_media_session_resource_stop_default(
    struct df_media_session *session, void *context) {
    (void)session;
    (void)context;
}

static char *df_media_session_manager_copy_string(const char *source) {
    size_t length;
    char *copy;

    if (source == NULL) return NULL;
    length = strlen(source) + 1U;
    copy = malloc(length);
    if (copy != NULL) memcpy(copy, source, length);
    return copy;
}

static void df_media_session_manager_free_config(
    struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return;
    if (manager->stations != NULL) {
        for (index = 0U; index < manager->config.station_count; index++) {
            free((void *)manager->stations[index].id);
            free((void *)manager->stations[index].stream_name);
        }
    }
    free(manager->stations);
    free(manager->go2rtc_host);
    free(manager->rtsp_username);
    free(manager->credentials_path);
    manager->stations = NULL;
    manager->go2rtc_host = NULL;
    manager->rtsp_username = NULL;
    manager->credentials_path = NULL;
}

static int df_media_session_manager_copy_config(
    struct df_media_session_manager *manager,
    const struct df_media_module_config_v3 *config) {
    size_t index;

    manager->config = *config;
    if (config->station_count > 0U) {
        manager->stations = calloc(config->station_count,
            sizeof(*manager->stations));
        if (manager->stations == NULL) return DF_ERR_IO;
    }
    manager->config.stations = manager->stations;
    for (index = 0U; index < config->station_count; index++) {
        manager->stations[index] = config->stations[index];
        manager->stations[index].id =
            df_media_session_manager_copy_string(config->stations[index].id);
        manager->stations[index].stream_name =
            df_media_session_manager_copy_string(
                config->stations[index].stream_name);
        if (manager->stations[index].id == NULL ||
            manager->stations[index].stream_name == NULL) return DF_ERR_IO;
    }

    manager->go2rtc_host =
        df_media_session_manager_copy_string(config->go2rtc_host);
    manager->rtsp_username =
        df_media_session_manager_copy_string(config->rtsp_username);
    manager->credentials_path =
        df_media_session_manager_copy_string(config->credentials_path);
    if ((config->go2rtc_host != NULL && manager->go2rtc_host == NULL) ||
        (config->rtsp_username != NULL && manager->rtsp_username == NULL) ||
        (config->credentials_path != NULL && manager->credentials_path == NULL))
        return DF_ERR_IO;
    manager->config.go2rtc_host = manager->go2rtc_host;
    manager->config.rtsp_username = manager->rtsp_username;
    manager->config.credentials_path = manager->credentials_path;
    return DF_OK;
}

static const struct df_media_station_config_v3 *
df_media_session_manager_find_station(
    const struct df_media_session_manager *manager, const char *station_id) {
    size_t index;

    if (manager == NULL || station_id == NULL) return NULL;
    for (index = 0U; index < manager->config.station_count; index++) {
        if (strcmp(manager->stations[index].id, station_id) == 0)
            return &manager->stations[index];
    }
    return NULL;
}

static struct df_media_session *df_media_session_manager_find_active(
    struct df_media_session_manager *manager, const char *station_id) {
    size_t index;

    if (manager == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (df_media_session_matches_station(&manager->sessions[index],
                station_id)) return &manager->sessions[index];
    }
    return NULL;
}

static struct df_media_session *df_media_session_manager_find_free(
    struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (!manager->sessions[index].active &&
            !manager->sessions[index].reserved)
            return &manager->sessions[index];
    }
    return NULL;
}

int df_media_session_manager_init(struct df_media_session_manager *manager,
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    size_t enabled_count = 0U;
    size_t requested;
    size_t index;

    if (manager == NULL) return DF_ERR_INVALID;
    memset(manager, 0, sizeof(*manager));
    if (df_media_module_config_v3_validate(config, callbacks) != DF_OK)
        return DF_ERR_INVALID;
    if (df_media_session_manager_copy_config(manager, config) != DF_OK) {
        df_media_session_manager_free_config(manager);
        memset(manager, 0, sizeof(*manager));
        return DF_ERR_IO;
    }
    manager->callbacks = *callbacks;
    for (index = 0U; index < config->station_count; index++) {
        if (config->stations[index].enabled) enabled_count++;
    }
    requested = config->max_encoders == 0U ? 1U : config->max_encoders;
    manager->capacity = config->enabled && requested < enabled_count ?
        requested : (config->enabled ? enabled_count : 0U);
    if (manager->capacity > 0U) {
        manager->sessions = calloc(manager->capacity,
            sizeof(*manager->sessions));
        if (manager->sessions == NULL) {
            df_media_session_manager_free_config(manager);
            memset(manager, 0, sizeof(*manager));
            return DF_ERR_IO;
        }
    }
    manager->resource_hooks.start =
        df_media_session_resource_start_default;
    manager->resource_hooks.stop = df_media_session_resource_stop_default;
    manager->initialized = true;
    return DF_OK;
}

void df_media_session_manager_destroy(struct df_media_session_manager *manager) {
    size_t index;

    if (manager == NULL) return;
    if (manager->sessions != NULL) {
        for (index = 0U; index < manager->capacity; index++) {
            if (manager->sessions[index].active ||
                manager->sessions[index].reserved)
                manager->resource_hooks.stop(&manager->sessions[index],
                    manager->resource_hooks.context);
            df_media_session_reset(&manager->sessions[index]);
        }
    }
    free(manager->sessions);
    df_media_session_manager_free_config(manager);
    memset(manager, 0, sizeof(*manager));
}

void df_media_session_manager_set_resource_hooks(
    struct df_media_session_manager *manager,
    const struct df_media_session_resource_hooks *hooks) {
    if (manager == NULL || !manager->initialized) return;
    if (hooks == NULL) {
        manager->resource_hooks.start =
            df_media_session_resource_start_default;
        manager->resource_hooks.stop = df_media_session_resource_stop_default;
        manager->resource_hooks.context = NULL;
        return;
    }
    manager->resource_hooks.start = hooks->start == NULL ?
        df_media_session_resource_start_default : hooks->start;
    manager->resource_hooks.stop = hooks->stop == NULL ?
        df_media_session_resource_stop_default : hooks->stop;
    manager->resource_hooks.context = hooks->context;
}

int df_media_session_manager_start(struct df_media_session_manager *manager,
    const char *station_id, enum df_media_session_purpose purpose,
    uint64_t now_ms, uint64_t *generation) {
    const struct df_media_station_config_v3 *station;
    struct df_media_session *session;
    uint64_t available_kib;
    uint64_t proposed_generation;
    uint32_t station_ipv4;
    int result;

    if (generation == NULL) return DF_ERR_INVALID;
    *generation = 0U;
    if (manager == NULL || !manager->initialized || station_id == NULL ||
        station_id[0] == '\0' ||
        (purpose != DF_MEDIA_SESSION_PREVIEW &&
         purpose != DF_MEDIA_SESSION_CALL)) return DF_ERR_INVALID;

    station = df_media_session_manager_find_station(manager, station_id);
    if (station == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!manager->config.enabled || !station->enabled)
        return DF_MEDIA_ERROR_STATION_DISABLED;
    session = df_media_session_manager_find_active(manager, station_id);
    if (session != NULL) {
        *generation = session->generation;
        return DF_OK;
    }
    if (manager->active_count >= manager->capacity)
        return DF_MEDIA_ERROR_CAPACITY_BUSY;
    session = df_media_session_manager_find_free(manager);
    if (session == NULL) return DF_MEDIA_ERROR_CAPACITY_BUSY;

    station_ipv4 = station->ipv4;
    if (station_ipv4 == 0U &&
        (manager->callbacks.resolve_route(station->logical_address, now_ms,
             &station_ipv4, manager->callbacks.context) != DF_OK ||
         station_ipv4 == 0U)) return DF_MEDIA_ERROR_ROUTE_UNAVAILABLE;
    if (manager->config.min_free_kib != 0U &&
        (manager->callbacks.available_memory(&available_kib,
             manager->callbacks.context) != DF_OK ||
         available_kib < manager->config.min_free_kib))
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;
    if (manager->next_generation == UINT64_MAX)
        return DF_MEDIA_ERROR_RESOURCE_EXHAUSTED;

    result = df_media_session_prepare(session, station, station_ipv4, purpose,
        now_ms);
    if (result != DF_OK) return DF_ERR_INVALID;
    proposed_generation = manager->next_generation + 1U;
    if (manager->resource_hooks.start(session, proposed_generation,
            manager->resource_hooks.context) != DF_OK) {
        manager->resource_hooks.stop(session,
            manager->resource_hooks.context);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    if (df_media_session_activate(session, proposed_generation) != DF_OK) {
        manager->resource_hooks.stop(session,
            manager->resource_hooks.context);
        df_media_session_reset(session);
        return DF_MEDIA_ERROR_ENCODER_FAILED;
    }
    manager->next_generation = proposed_generation;
    manager->active_count++;
    *generation = proposed_generation;
    return DF_OK;
}

int df_media_session_manager_command(struct df_media_session_manager *manager,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    const struct df_media_station_config_v3 *station;
    struct df_media_session *session;
    int result;

    if (manager == NULL || !manager->initialized || key == NULL ||
        key->station_id == NULL) return DF_ERR_INVALID;
    if (command != DF_MEDIA_MODULE_COMMAND_STOP &&
        command != DF_MEDIA_MODULE_COMMAND_VIEWER) return DF_ERR_INVALID;
    station = df_media_session_manager_find_station(manager, key->station_id);
    if (station == NULL) return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!manager->config.enabled || !station->enabled)
        return DF_MEDIA_ERROR_STATION_DISABLED;
    session = df_media_session_manager_find_active(manager, key->station_id);
    if (session == NULL || session->generation != key->generation ||
        key->generation == 0U) return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    if (command == DF_MEDIA_MODULE_COMMAND_STOP) {
        manager->resource_hooks.stop(session,
            manager->resource_hooks.context);
        result = df_media_session_command(session, command, key, active,
            now_ms);
        if (result != DF_OK) return result;
        df_media_session_reset(session);
        manager->active_count--;
        return DF_OK;
    }
    return df_media_session_command(session, command, key, active, now_ms);
}

size_t df_media_session_manager_active(
    const struct df_media_session_manager *manager) {
    return manager == NULL || !manager->initialized ? 0U :
        manager->active_count;
}

size_t df_media_session_manager_capacity(
    const struct df_media_session_manager *manager) {
    return manager == NULL || !manager->initialized ? 0U : manager->capacity;
}

const struct df_media_session *df_media_session_manager_lookup(
    const struct df_media_session_manager *manager,
    const struct df_media_session_key *key) {
    size_t index;

    if (manager == NULL || !manager->initialized || key == NULL) return NULL;
    for (index = 0U; index < manager->capacity; index++) {
        if (df_media_session_matches_key(&manager->sessions[index], key))
            return &manager->sessions[index];
    }
    return NULL;
}
