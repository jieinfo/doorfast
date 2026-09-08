#ifndef DOORFAST_RUNTIME_SERVICE_H
#define DOORFAST_RUNTIME_SERVICE_H

#include "runtime_config.h"

typedef int (*df_runtime_delay_slice_fn)(unsigned delay_ms, void *context);

int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms,
                          df_runtime_delay_slice_fn run_slice,
                          void *context);
int df_runtime_service_run(const struct df_runtime_config *runtime);

#endif
