#ifndef DOORFAST_CAPTURE_H
#define DOORFAST_CAPTURE_H

#include <stdbool.h>

#include "doorfast.h"

struct df_capture;

const char *df_capture_default_filter(void);
int df_capture_open(const char *device, bool promiscuous, struct df_capture **capture);
int df_capture_set_filter(struct df_capture *capture, const char *bpf);
void df_capture_close(struct df_capture *capture);

#endif
