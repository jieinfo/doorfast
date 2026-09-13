#ifndef DOORFAST_GVS_VENDOR_HEADER_H
#define DOORFAST_GVS_VENDOR_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "gvs_serialize.h"

typedef int (*df_gvs_random_fill_fn)(uint8_t *output, size_t length,
                                     void *context);

struct df_gvs_vendor_header_context {
    df_gvs_random_fill_fn fill_random;
    void *random_context;
};

int df_gvs_udp_transform(
    const uint8_t input[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t output[DF_GVS_HEADER_FIELD_SIZE]);

int df_gvs_vendor_header_fields(
    const struct df_gvs_header_request *request,
    uint8_t random_code[DF_GVS_HEADER_FIELD_SIZE],
    uint8_t encryption_code[DF_GVS_HEADER_FIELD_SIZE], void *context);

#endif
