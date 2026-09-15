#ifndef DOORFAST_RUNTIME_ID_H
#define DOORFAST_RUNTIME_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_RUNTIME_ID_HEX_LENGTH 16U

typedef int (*df_runtime_id_fill_fn)(uint8_t *, size_t, void *);

int df_runtime_id_generate(char output[DF_RUNTIME_ID_HEX_LENGTH + 1U],
    df_runtime_id_fill_fn fill, void *context);
bool df_runtime_id_is_valid(const char *value);

#endif
