#ifndef DOORFAST_SIP_H
#define DOORFAST_SIP_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "event.h"

int df_sip_parse(const uint8_t *data, size_t length, struct df_event *event);

#endif
