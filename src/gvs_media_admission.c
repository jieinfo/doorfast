#include "gvs_media_admission.h"

#include <string.h>

enum df_gvs_media_admission df_gvs_media_admit(
    const struct df_gvs_session *session, const uint8_t identity[6],
    const uint8_t destination[6], const uint8_t source[6])
{
    if (session == NULL || identity == NULL || destination == NULL ||
        source == NULL) {
        return DF_GVS_MEDIA_ADMISSION_ERROR;
    }
    if (session->generation == 0U ||
        (session->state != DF_GVS_PREVIEW &&
         session->state != DF_GVS_RINGING &&
         session->state != DF_GVS_TALKING)) {
        return DF_GVS_MEDIA_REJECT_INACTIVE;
    }
    if (memcmp(destination, identity, 6) != 0) {
        return DF_GVS_MEDIA_REJECT_DESTINATION;
    }
    if (memcmp(source, session->peer, 6) != 0) {
        return DF_GVS_MEDIA_REJECT_PEER;
    }
    return DF_GVS_MEDIA_ACCEPTED;
}
