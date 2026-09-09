#ifndef DOORFAST_GVS_PRIORITY_H
#define DOORFAST_GVS_PRIORITY_H
#include <stdint.h>

/* Abstract SDK categories, not packet fields or authenticated identities.
 * Only meaningful for deciding between distinct callers while ringing. */
enum df_gvs_priority_decision {
    DF_GVS_PRIORITY_UNSUPPORTED = -1,
    DF_GVS_PRIORITY_KEEP = 0,
    DF_GVS_PRIORITY_PREEMPT = 1
};

enum df_gvs_priority_decision df_gvs_priority_compare(int current, int incoming);
/* Static SDK address classification only; never an authentication decision.
 * Returns -1 for unknown types or null inputs. */
int df_gvs_call_category(const uint8_t peer[6], const uint8_t local[6]);

#endif
