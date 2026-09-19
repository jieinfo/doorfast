#include "media_module.h"

#include <string.h>

#include "doorfast.h"

int df_media_module_config_v3_validate(
    const struct df_media_module_config_v3 *config,
    const struct df_media_module_callbacks_v3 *callbacks) {
    size_t enabled_count = 0U;
    size_t capacity;
    size_t index;
    size_t prior;

    if (config == NULL || callbacks == NULL ||
        callbacks->emit_control == NULL || callbacks->resolve_route == NULL ||
        callbacks->available_memory == NULL ||
        (config->incoming_call_policy !=
            DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW &&
         config->incoming_call_policy != DF_MEDIA_CALL_PRESERVE_PREVIEWS))
        return DF_ERR_INVALID;
    if (config->station_count > 0U && config->stations == NULL)
        return DF_ERR_INVALID;
    for (index = 0U; index < config->station_count; index++) {
        const struct df_media_station_config_v3 *station =
            &config->stations[index];

        if (station->id == NULL || station->id[0] == '\0' ||
            strlen(station->id) >= DF_MEDIA_MODULE_STATION_ID_MAX ||
            station->stream_name == NULL || station->stream_name[0] == '\0')
            return DF_ERR_INVALID;
        for (prior = 0U; prior < index; prior++) {
            if (strcmp(station->id, config->stations[prior].id) == 0)
                return DF_ERR_INVALID;
        }
        if (station->enabled) enabled_count++;
    }
    if (!config->enabled) return DF_OK;
    capacity = config->max_encoders == 0U ? 1U : config->max_encoders;
    if (enabled_count == 0U || capacity > enabled_count)
        return DF_ERR_INVALID;
    return DF_OK;
}
