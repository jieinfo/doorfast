#ifndef DF_DEPLOYMENT_SNAPSHOT_H
#define DF_DEPLOYMENT_SNAPSHOT_H

#include "deployment_preflight.h"

int df_deployment_snapshot_collect(const struct df_deployment_config *config,
                                   const char *root,
                                   struct df_deployment_snapshot *snapshot);

#endif
