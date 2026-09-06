#ifndef DOORFAST_AUDIT_H
#define DOORFAST_AUDIT_H

#include <stddef.h>

#include "event.h"
#include "policy.h"

void df_audit_format(char *dst, size_t dst_size, const struct df_event *event,
                     enum df_decision decision);
void df_audit_event(const struct df_event *event, enum df_decision decision);

#endif
