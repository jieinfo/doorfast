#ifndef DOORFAST_GVS_SESSION_H
#define DOORFAST_GVS_SESSION_H

#include <stdint.h>

#include "doorfast.h"
#include "event.h"
#include "gvs_frame.h"

enum df_gvs_session_state {
    DF_GVS_IDLE = 0,
    DF_GVS_PREVIEW,
    DF_GVS_RINGING,
    DF_GVS_TALKING,
    DF_GVS_ENDED,
};

struct df_gvs_session {
    enum df_gvs_session_state state;
    uint8_t peer[6];
};

int df_gvs_session_apply(struct df_gvs_session *session, const struct df_gvs_frame *frame,
                         const struct df_event *event);

#endif
