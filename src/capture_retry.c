#include "capture_retry.h"

#include <string.h>

#define DF_CAPTURE_RETRY_LIMIT 5U
#define DF_CAPTURE_RETRY_BASE_MS 250U

int df_capture_retry_next(struct df_capture_retry *retry, unsigned *delay_ms) {
    if (retry == NULL || delay_ms == NULL) {
        return DF_ERR_INVALID;
    }
    if (retry->attempts >= DF_CAPTURE_RETRY_LIMIT) {
        return DF_ERR_IO;
    }
    *delay_ms = DF_CAPTURE_RETRY_BASE_MS << retry->attempts;
    retry->attempts++;
    return DF_OK;
}

void df_capture_retry_reset(struct df_capture_retry *retry) {
    if (retry != NULL) {
        memset(retry, 0, sizeof(*retry));
    }
}
