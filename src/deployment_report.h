#ifndef DF_DEPLOYMENT_REPORT_H
#define DF_DEPLOYMENT_REPORT_H

#include <stdio.h>

#include "deployment_preflight.h"

int df_deployment_report_write_json(FILE *stream,
                                    const struct df_deployment_config *config,
                                    const struct df_deployment_report *report);

#endif
