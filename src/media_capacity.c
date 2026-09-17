#include "media_capacity.h"

#include "doorfast.h"

unsigned df_media_effective_capacity(unsigned requested, unsigned resource_limit,
                                     unsigned protocol_limit)
{
    unsigned capacity;

    if (resource_limit == 0U || protocol_limit == 0U) return 0U;
    if (requested == 0U) requested = 1U;
    capacity = requested < resource_limit ? requested : resource_limit;
    return capacity < protocol_limit ? capacity : protocol_limit;
}

int df_media_encoder_select(enum df_media_encoder requested,
                            const struct df_media_encoder_probe *probe,
                            enum df_media_encoder *selected)
{
    if (probe == NULL || selected == NULL || requested < DF_MEDIA_ENCODER_AUTO ||
        requested > DF_MEDIA_ENCODER_QSV) return DF_ERR_INVALID;
    if (requested == DF_MEDIA_ENCODER_AUTO) {
        if (probe->qsv_available) *selected = DF_MEDIA_ENCODER_QSV;
        else if (probe->vaapi_available) *selected = DF_MEDIA_ENCODER_VAAPI;
        else if (probe->software_available) *selected = DF_MEDIA_ENCODER_SOFTWARE;
        else return DF_ERR_INVALID;
        return DF_OK;
    }
    if ((requested == DF_MEDIA_ENCODER_QSV && !probe->qsv_available) ||
        (requested == DF_MEDIA_ENCODER_VAAPI && !probe->vaapi_available) ||
        (requested == DF_MEDIA_ENCODER_SOFTWARE && !probe->software_available))
        return DF_ERR_INVALID;
    *selected = requested;
    return DF_OK;
}
