#ifndef DOORFAST_CAPTURE_RETRY_H
#define DOORFAST_CAPTURE_RETRY_H

#include "doorfast.h"

struct df_capture_retry {
    unsigned attempts;
};

int df_capture_retry_next(struct df_capture_retry *retry, unsigned *delay_ms);
void df_capture_retry_reset(struct df_capture_retry *retry);

#endif
