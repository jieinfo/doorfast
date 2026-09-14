#include "gvs_media_lifecycle.h"

#include <string.h>

#include "doorfast.h"

static bool session_has_media(const struct df_gvs_session *session)
{
    return session->generation != 0U &&
        (session->state == DF_GVS_PREVIEW ||
         session->state == DF_GVS_RINGING ||
         session->state == DF_GVS_TALKING);
}

void df_gvs_media_lifecycle_init(struct df_gvs_media_lifecycle *lifecycle)
{
    if (lifecycle != NULL) {
        memset(lifecycle, 0, sizeof(*lifecycle));
    }
}

int df_gvs_media_lifecycle_sync(struct df_gvs_media_lifecycle *lifecycle,
    const struct df_gvs_session *session,
    struct df_gvs_media_lifecycle_result *result)
{
    bool active;
    bool generation_changed;

    if (lifecycle == NULL || session == NULL || result == NULL) {
        return DF_ERR_INVALID;
    }
    memset(result, 0, sizeof(*result));
    active = session_has_media(session);
    generation_changed = lifecycle->active && active &&
        lifecycle->generation != session->generation;

    result->active = active;
    result->generation = active ? session->generation : 0U;
    result->started = active && (!lifecycle->active || generation_changed);
    result->ended = lifecycle->active && (!active || generation_changed);
    result->clear = !lifecycle->initialized || result->started ||
        result->ended;

    lifecycle->generation = result->generation;
    lifecycle->active = active;
    lifecycle->initialized = true;
    return DF_OK;
}
