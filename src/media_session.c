#include "media_session.h"

#include <stdio.h>
#include <string.h>

#include "doorfast.h"

void df_media_session_reset(struct df_media_session *session) {
    if (session != NULL) memset(session, 0, sizeof(*session));
}
int df_media_session_prepare(struct df_media_session *session,
    const struct df_media_station_config_v3 *station, uint32_t station_ipv4,
    enum df_media_session_purpose purpose, uint64_t now_ms) {
    if (session == NULL || station == NULL || station->id == NULL ||
        station->stream_name == NULL || station_ipv4 == 0U ||
        strlen(station->id) >= sizeof(session->station_id) ||
        strlen(station->stream_name) >= sizeof(session->stream_name) ||
        (purpose != DF_MEDIA_SESSION_PREVIEW &&
         purpose != DF_MEDIA_SESSION_CALL)) return DF_ERR_INVALID;

    df_media_session_reset(session);
    (void)snprintf(session->station_id, sizeof(session->station_id), "%s",
        station->id);
    (void)snprintf(session->stream_name, sizeof(session->stream_name), "%s",
        station->stream_name);
    memcpy(session->station, station->logical_address,
        sizeof(session->station));
    session->station_ipv4 = station_ipv4;
    session->purpose = purpose;
    session->state = DF_MEDIA_SESSION_IDLE;
    session->last_error = DF_MEDIA_ERROR_NONE;
    session->reserved = true;
    session->started_ms = now_ms;
    return DF_OK;
}

int df_media_session_activate(struct df_media_session *session,
    uint64_t generation) {
    if (session == NULL || !session->reserved || session->active ||
        generation == 0U) return DF_ERR_INVALID;
    session->generation = generation;
    session->state = DF_MEDIA_SESSION_REQUESTING;
    session->reserved = false;
    session->active = true;
    return DF_OK;
}

int df_media_session_publish(struct df_media_session *session,
    const struct df_media_station_config_v3 *station, uint32_t station_ipv4,
    enum df_media_session_purpose purpose, uint64_t generation,
    uint64_t now_ms) {
    int result = df_media_session_prepare(session, station, station_ipv4,
        purpose, now_ms);

    if (result != DF_OK) return result;
    result = df_media_session_activate(session, generation);
    if (result != DF_OK) df_media_session_reset(session);
    return result;
}

bool df_media_session_matches_station(const struct df_media_session *session,
    const char *station_id) {
    return session != NULL && station_id != NULL && session->active &&
        strcmp(session->station_id, station_id) == 0;
}

bool df_media_session_matches_key(const struct df_media_session *session,
    const struct df_media_session_key *key) {
    return key != NULL && key->generation != 0U &&
        df_media_session_matches_station(session, key->station_id) &&
        session->generation == key->generation;
}

int df_media_session_command(struct df_media_session *session,
    enum df_media_module_command command,
    const struct df_media_session_key *key, bool active, uint64_t now_ms) {
    (void)now_ms;
    if (session == NULL || key == NULL || key->station_id == NULL)
        return DF_ERR_INVALID;
    if (!df_media_session_matches_station(session, key->station_id))
        return DF_MEDIA_ERROR_STATION_NOT_FOUND;
    if (!df_media_session_matches_key(session, key))
        return DF_MEDIA_ERROR_GENERATION_MISMATCH;
    if (command == DF_MEDIA_MODULE_COMMAND_STOP) {
        session->state = DF_MEDIA_SESSION_STOPPING;
        session->active = false;
        session->viewer_active = false;
        return DF_OK;
    }
    if (command == DF_MEDIA_MODULE_COMMAND_VIEWER) {
        session->viewer_active = active;
        if (active) session->state = DF_MEDIA_SESSION_VIEWING;
        else if (session->state == DF_MEDIA_SESSION_VIEWING)
            session->state = DF_MEDIA_SESSION_REQUESTING;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}
