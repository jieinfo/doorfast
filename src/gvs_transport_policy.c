#include "gvs_transport_policy.h"

#include <stddef.h>

int df_gvs_transport_policy_validate(
    const struct df_gvs_transport_policy *policy) {
    if (policy == NULL) {
        return DF_ERR_INVALID;
    }
    /* Passive mode is a hard boundary, not a hint. */
    if (policy->passive_only && policy->real_send_requested) {
        return DF_ERR_INVALID;
    }
    if (policy->real_send_requested &&
        (!policy->one_shot || !policy->rollback_ready)) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}
