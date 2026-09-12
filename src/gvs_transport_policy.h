#ifndef DOORFAST_GVS_TRANSPORT_POLICY_H
#define DOORFAST_GVS_TRANSPORT_POLICY_H

#include <stdbool.h>

#include "doorfast.h"

/*
 * The policy gate is deliberately independent from any socket implementation.
 * A production configuration remains passive until every precondition is true.
 */
struct df_gvs_transport_policy {
    bool passive_only;
    bool real_send_requested;
    bool one_shot;
    bool rollback_ready;
};

int df_gvs_transport_policy_validate(
    const struct df_gvs_transport_policy *policy);

#endif
