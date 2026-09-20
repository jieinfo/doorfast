#ifndef DOORFAST_GVS_MONITOR_H
#define DOORFAST_GVS_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_frame.h"

#define DF_GVS_MONITOR_REQUEST_INTERVAL_MS 1000U
#define DF_GVS_MONITOR_MAX_REQUESTS 3U
#define DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS 8000U
#define DF_GVS_MONITOR_STOP_TIMEOUT_MS 1000U

enum df_gvs_monitor_state {
    DF_GVS_MONITOR_IDLE = 0,
    DF_GVS_MONITOR_REQUESTING,
    DF_GVS_MONITOR_AWAITING_VIDEO,
    DF_GVS_MONITOR_PUBLISHING,
    DF_GVS_MONITOR_VIEWING,
    DF_GVS_MONITOR_STOPPING,
    DF_GVS_MONITOR_FAILED,
};

enum df_gvs_monitor_failure {
    DF_GVS_MONITOR_FAILURE_NONE = 0,
    DF_GVS_MONITOR_TIMEOUT,
    DF_GVS_MONITOR_UNCONFIRMED,
    DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT,
    DF_GVS_MONITOR_STOP_TIMEOUT,
};

struct df_gvs_monitor_action {
    bool send;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t family;
    uint8_t opcode;
    uint8_t payload[7];
    size_t payload_length;
    uint64_t generation;
};

struct df_gvs_monitor_result {
    bool confirmed;
    bool keepalive_reply;
    bool media_ready;
    bool stopped;
    bool failed;
};

struct df_gvs_monitor {
    enum df_gvs_monitor_state state;
    enum df_gvs_monitor_failure failure;
    uint8_t local[6];
    uint8_t station[6];
    uint32_t station_ipv4;
    uint64_t generation;
    uint64_t last_now_ms;
    uint64_t next_action_ms;
    uint64_t first_frame_deadline_ms;
    uint64_t first_frame_timeout_ms;
    unsigned request_attempts;
    bool media_ready;
    bool stop_sent;
};

void df_gvs_monitor_init(struct df_gvs_monitor *);
int df_gvs_monitor_set_first_frame_timeout(struct df_gvs_monitor *,
    uint64_t timeout_ms);
int df_gvs_monitor_start(struct df_gvs_monitor *, const uint8_t local[6],
    const uint8_t station[6], uint32_t station_ipv4, uint64_t now_ms);
int df_gvs_monitor_start_with_generation(struct df_gvs_monitor *,
    const uint8_t local[6], const uint8_t station[6], uint32_t station_ipv4,
    uint64_t generation, uint64_t now_ms);
int df_gvs_monitor_cancel(struct df_gvs_monitor *, uint64_t now_ms);
int df_gvs_monitor_bind_call(struct df_gvs_monitor *, const uint8_t local[6],
    const uint8_t station[6], uint32_t station_ipv4, uint64_t generation,
    uint64_t now_ms);
int df_gvs_monitor_step(struct df_gvs_monitor *, uint64_t now_ms,
    struct df_gvs_monitor_action *);
int df_gvs_monitor_receive(struct df_gvs_monitor *, const struct df_gvs_frame *,
    uint32_t source_ipv4, uint64_t now_ms, struct df_gvs_monitor_result *);
int df_gvs_monitor_admit_jpeg(struct df_gvs_monitor *, const uint8_t source[6],
    const uint8_t destination[6], uint32_t source_ipv4, uint64_t generation,
    uint64_t now_ms, struct df_gvs_monitor_result *);
int df_gvs_monitor_mark_publishing(struct df_gvs_monitor *, uint64_t generation,
    uint64_t now_ms);
int df_gvs_monitor_set_viewing(struct df_gvs_monitor *, uint64_t generation,
    bool active, uint64_t now_ms);
int df_gvs_monitor_stop(struct df_gvs_monitor *, uint64_t generation,
    uint64_t now_ms);

#endif
