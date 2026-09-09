#ifndef DOORFAST_CAPTURE_H
#define DOORFAST_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

struct df_capture;

enum df_capture_result {
    DF_CAPTURE_ERROR = -1,
    DF_CAPTURE_TIMEOUT = 0,
    DF_CAPTURE_PACKET = 1
};

const char *df_capture_default_filter(void);
int df_capture_open(const char *device, bool promiscuous, struct df_capture **capture);
int df_capture_set_filter(struct df_capture *capture, const char *bpf);
int df_capture_next(struct df_capture *capture, const uint8_t **packet, size_t *length);
void df_capture_close(struct df_capture *capture);

#endif
