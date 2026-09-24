#include "gvs_monitor.h"

#include <limits.h>
#include <string.h>

#include "gvs_station.h"

static const uint8_t df_gvs_monitor_request[] = {
    0x02, 0x20, 0x6f, 0x00, 0x20, 0x6e, 0x1e,
};

static const uint8_t df_gvs_monitor_confirmation[] = {0x1e, 0x00, 0x01};

static bool df_gvs_monitor_nonzero(const uint8_t address[6]) {
    size_t index;

    if (address == NULL) {
        return false;
    }
    for (index = 0; index < 6U; index++) {
        if (address[index] != 0U) {
            return true;
        }
    }
    return false;
}

static bool df_gvs_monitor_active(enum df_gvs_monitor_state state) {
    return state == DF_GVS_MONITOR_REQUESTING ||
        state == DF_GVS_MONITOR_AWAITING_VIDEO ||
        state == DF_GVS_MONITOR_PUBLISHING ||
        state == DF_GVS_MONITOR_VIEWING;
}

static void df_gvs_monitor_fail(struct df_gvs_monitor *monitor,
    enum df_gvs_monitor_failure failure) {
    monitor->state = DF_GVS_MONITOR_FAILED;
    monitor->failure = failure;
    monitor->next_action_ms = 0U;
    monitor->first_frame_deadline_ms = 0U;
    monitor->status_retry_deadline_ms = 0U;
    monitor->media_ready = false;
    monitor->status_retry_pending = false;
    monitor->retry_waiting = false;
}

void df_gvs_monitor_init(struct df_gvs_monitor *monitor) {
    if (monitor != NULL) {
        memset(monitor, 0, sizeof(*monitor));
        monitor->first_frame_timeout_ms =
            DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS;
    }
}

int df_gvs_monitor_set_first_frame_timeout(struct df_gvs_monitor *monitor,
    uint64_t timeout_ms) {
    if (monitor == NULL || timeout_ms == 0U ||
        df_gvs_monitor_active(monitor->state)) return DF_ERR_INVALID;
    monitor->first_frame_timeout_ms = timeout_ms;
    return DF_OK;
}

int df_gvs_monitor_set_persistent(struct df_gvs_monitor *monitor,
    bool persistent) {
    if (monitor == NULL ||
        (monitor->state != DF_GVS_MONITOR_IDLE &&
         monitor->state != DF_GVS_MONITOR_FAILED))
        return DF_ERR_INVALID;
    monitor->persistent = persistent;
    return DF_OK;
}

int df_gvs_monitor_start_with_generation(struct df_gvs_monitor *monitor,
    const uint8_t local[6], const uint8_t station[6], uint32_t station_ipv4,
    uint64_t generation, uint64_t now_ms) {
    uint64_t first_frame_timeout_ms;
    bool persistent;

    if (monitor == NULL || !df_gvs_monitor_nonzero(local) ||
        df_gvs_station_validate(station) != DF_OK || station_ipv4 == 0U ||
        generation == 0U ||
        (monitor->state != DF_GVS_MONITOR_IDLE &&
         monitor->state != DF_GVS_MONITOR_FAILED) ||
        (monitor->generation != 0U && now_ms < monitor->last_now_ms) ||
        monitor->generation == UINT64_MAX) {
        return DF_ERR_INVALID;
    }
    first_frame_timeout_ms = monitor->first_frame_timeout_ms == 0U ?
        DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS :
        monitor->first_frame_timeout_ms;
    persistent = monitor->persistent;
    memset(monitor, 0, sizeof(*monitor));
    monitor->first_frame_timeout_ms = first_frame_timeout_ms;
    monitor->persistent = persistent;
    monitor->state = DF_GVS_MONITOR_REQUESTING;
    memcpy(monitor->local, local, sizeof(monitor->local));
    memcpy(monitor->station, station, sizeof(monitor->station));
    monitor->station_ipv4 = station_ipv4;
    monitor->generation = generation;
    monitor->last_now_ms = now_ms;
    monitor->status_retry_deadline_ms = 0U;
    monitor->next_action_ms = now_ms;
    return DF_OK;
}

int df_gvs_monitor_start(struct df_gvs_monitor *monitor,
    const uint8_t local[6], const uint8_t station[6], uint32_t station_ipv4,
    uint64_t now_ms) {
    uint64_t generation;

    if (monitor == NULL || monitor->generation == UINT64_MAX) return DF_ERR_INVALID;
    generation = monitor->generation + 1U;
    return df_gvs_monitor_start_with_generation(monitor, local, station,
        station_ipv4, generation, now_ms);
}

int df_gvs_monitor_cancel(struct df_gvs_monitor *monitor, uint64_t now_ms) {
    if (monitor == NULL || now_ms < monitor->last_now_ms) return DF_ERR_INVALID;
    monitor->last_now_ms = now_ms;
    monitor->state = DF_GVS_MONITOR_IDLE;
    monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
    monitor->next_action_ms = 0U;
    monitor->first_frame_deadline_ms = 0U;
    monitor->status_retry_deadline_ms = 0U;
    monitor->request_attempts = 0U;
    monitor->media_ready = false;
    monitor->status_retry_pending = false;
    monitor->retry_waiting = false;
    monitor->stop_sent = false;
    return DF_OK;
}

int df_gvs_monitor_bind_call(struct df_gvs_monitor *monitor,
    const uint8_t local[6], const uint8_t station[6], uint32_t station_ipv4,
    uint64_t generation, uint64_t now_ms) {
    uint64_t first_frame_timeout_ms;

    if (monitor == NULL || !df_gvs_monitor_nonzero(local) ||
        df_gvs_station_validate(station) != DF_OK || station_ipv4 == 0U ||
        generation == 0U || now_ms < monitor->last_now_ms)
        return DF_ERR_INVALID;
    first_frame_timeout_ms = monitor->first_frame_timeout_ms == 0U ?
        DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT_MS :
        monitor->first_frame_timeout_ms;
    if (now_ms > UINT64_MAX - first_frame_timeout_ms)
        return DF_ERR_INVALID;
    memset(monitor, 0, sizeof(*monitor));
    monitor->first_frame_timeout_ms = first_frame_timeout_ms;
    monitor->state = DF_GVS_MONITOR_AWAITING_VIDEO;
    memcpy(monitor->local, local, sizeof(monitor->local));
    memcpy(monitor->station, station, sizeof(monitor->station));
    monitor->station_ipv4 = station_ipv4;
    monitor->generation = generation;
    monitor->last_now_ms = now_ms;
    monitor->first_frame_deadline_ms = now_ms + first_frame_timeout_ms;
    return DF_OK;
}

int df_gvs_monitor_step(struct df_gvs_monitor *monitor, uint64_t now_ms,
    struct df_gvs_monitor_action *action) {
    if (monitor == NULL || action == NULL || now_ms < monitor->last_now_ms) {
        return DF_ERR_INVALID;
    }
    memset(action, 0, sizeof(*action));
    monitor->last_now_ms = now_ms;

    if (monitor->status_retry_pending &&
        monitor->status_retry_deadline_ms != 0U &&
        now_ms >= monitor->status_retry_deadline_ms) {
        monitor->state = DF_GVS_MONITOR_REQUESTING;
        monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
        monitor->next_action_ms = now_ms;
        monitor->first_frame_deadline_ms = 0U;
        monitor->status_retry_deadline_ms = 0U;
        monitor->media_ready = false;
        monitor->status_retry_pending = false;
        monitor->retry_waiting = false;
        monitor->stop_sent = false;
        return DF_OK;
    }

    if (monitor->state == DF_GVS_MONITOR_REQUESTING) {
        if (now_ms < monitor->next_action_ms) {
            return DF_OK;
        }
        if (!monitor->persistent &&
            monitor->request_attempts >= DF_GVS_MONITOR_MAX_REQUESTS) {
            df_gvs_monitor_fail(monitor, DF_GVS_MONITOR_TIMEOUT);
            return DF_OK;
        }
        action->send = true;
        action->family = 0x03U;
        action->opcode = 0x04U;
        action->payload_length = sizeof(df_gvs_monitor_request);
        memcpy(action->destination, monitor->station,
               sizeof(action->destination));
        memcpy(action->source, monitor->local, sizeof(action->source));
        memcpy(action->payload, df_gvs_monitor_request,
               sizeof(df_gvs_monitor_request));
        action->generation = monitor->generation;
        if (monitor->request_attempts != UINT_MAX)
            monitor->request_attempts++;
        monitor->retry_waiting = false;
        if (now_ms > UINT64_MAX - DF_GVS_MONITOR_REQUEST_INTERVAL_MS) {
            df_gvs_monitor_fail(monitor, DF_GVS_MONITOR_TIMEOUT);
        } else {
            monitor->next_action_ms = now_ms + DF_GVS_MONITOR_REQUEST_INTERVAL_MS;
        }
        return DF_OK;
    }
    if (monitor->state == DF_GVS_MONITOR_AWAITING_VIDEO &&
        monitor->first_frame_deadline_ms != 0U &&
        !monitor->media_ready && now_ms >= monitor->first_frame_deadline_ms) {
        if (!monitor->persistent) {
            df_gvs_monitor_fail(monitor, DF_GVS_MONITOR_FIRST_FRAME_TIMEOUT);
        } else {
            monitor->state = DF_GVS_MONITOR_REQUESTING;
            monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
            monitor->next_action_ms = now_ms;
            monitor->first_frame_deadline_ms = 0U;
            monitor->media_ready = false;
            monitor->retry_waiting = false;
            monitor->stop_sent = false;
        }
        return DF_OK;
    }
    if (monitor->state == DF_GVS_MONITOR_STOPPING) {
        if (!monitor->stop_sent) {
            action->send = true;
            action->family = 0x03U;
            action->opcode = 0x02U;
            action->payload_length = 1U;
            memcpy(action->destination, monitor->station,
                   sizeof(action->destination));
            memcpy(action->source, monitor->local, sizeof(action->source));
            action->generation = monitor->generation;
            monitor->stop_sent = true;
            if (now_ms > UINT64_MAX - DF_GVS_MONITOR_STOP_TIMEOUT_MS) {
                monitor->state = DF_GVS_MONITOR_IDLE;
                monitor->failure = DF_GVS_MONITOR_STOP_TIMEOUT;
            } else {
                monitor->next_action_ms = now_ms + DF_GVS_MONITOR_STOP_TIMEOUT_MS;
            }
            return DF_OK;
        }
        if (now_ms >= monitor->next_action_ms) {
            monitor->state = DF_GVS_MONITOR_IDLE;
            monitor->failure = DF_GVS_MONITOR_STOP_TIMEOUT;
            monitor->media_ready = false;
        }
    }
    return DF_OK;
}

static int df_gvs_monitor_match(const struct df_gvs_monitor *monitor,
    const struct df_gvs_frame *frame, uint32_t source_ipv4) {
    if (monitor == NULL || frame == NULL || frame->family != 0x03U ||
        source_ipv4 != monitor->station_ipv4 ||
        memcmp(frame->source, monitor->station, sizeof(monitor->station)) != 0 ||
        memcmp(frame->destination, monitor->local, sizeof(monitor->local)) != 0) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_gvs_monitor_receive(struct df_gvs_monitor *monitor,
    const struct df_gvs_frame *frame, uint32_t source_ipv4, uint64_t now_ms,
    struct df_gvs_monitor_result *result) {
    if (monitor == NULL || frame == NULL || result == NULL ||
        now_ms < monitor->last_now_ms ||
        df_gvs_monitor_match(monitor, frame, source_ipv4) != DF_OK) {
        return DF_ERR_INVALID;
    }
    memset(result, 0, sizeof(*result));
    if (monitor->state == DF_GVS_MONITOR_REQUESTING &&
        !monitor->retry_waiting && frame->opcode == 0x84U) {
        if (frame->payload_length != sizeof(df_gvs_monitor_confirmation) ||
            frame->payload == NULL || memcmp(frame->payload,
                df_gvs_monitor_confirmation, sizeof(df_gvs_monitor_confirmation)) != 0 ||
            now_ms > UINT64_MAX - monitor->first_frame_timeout_ms) {
            return DF_ERR_INVALID;
        }
        monitor->last_now_ms = now_ms;
        monitor->state = DF_GVS_MONITOR_AWAITING_VIDEO;
        monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
        monitor->first_frame_deadline_ms =
            now_ms + monitor->first_frame_timeout_ms;
        result->confirmed = true;
        return DF_OK;
    }
    if (monitor->state == DF_GVS_MONITOR_REQUESTING &&
        !monitor->retry_waiting && frame->opcode == 0x50U) {
        if (frame->payload_length != 0U) return DF_ERR_INVALID;
        monitor->last_now_ms = now_ms;
        return DF_OK;
    }
    if ((monitor->state == DF_GVS_MONITOR_AWAITING_VIDEO ||
         monitor->state == DF_GVS_MONITOR_PUBLISHING ||
         monitor->state == DF_GVS_MONITOR_VIEWING) &&
        frame->opcode == 0x51U) {
        if (frame->payload_length != 0U) return DF_ERR_INVALID;
        monitor->last_now_ms = now_ms;
        result->keepalive_reply = true;
        return DF_OK;
    }
    if ((monitor->state == DF_GVS_MONITOR_REQUESTING ||
         monitor->state == DF_GVS_MONITOR_AWAITING_VIDEO ||
         monitor->state == DF_GVS_MONITOR_PUBLISHING ||
         monitor->state == DF_GVS_MONITOR_VIEWING) &&
        frame->opcode == 0x02U) {
        if (frame->payload_length != 1U || frame->payload == NULL ||
            frame->payload[0] > 1U ||
            now_ms > UINT64_MAX - DF_GVS_MONITOR_REQUEST_INTERVAL_MS)
            return DF_ERR_INVALID;
        monitor->last_now_ms = now_ms;
        if (frame->payload[0] == 1U) {
            if (monitor->persistent &&
                (monitor->state == DF_GVS_MONITOR_PUBLISHING ||
                 monitor->state == DF_GVS_MONITOR_VIEWING)) {
                if (now_ms > UINT64_MAX -
                    DF_GVS_MONITOR_STATUS_RETRY_DELAY_MS)
                    return DF_ERR_INVALID;
                if (!monitor->status_retry_pending) {
                    monitor->status_retry_pending = true;
                    monitor->status_retry_deadline_ms = now_ms +
                        DF_GVS_MONITOR_STATUS_RETRY_DELAY_MS;
                }
            }
            return DF_OK;
        }
        if (!monitor->retry_waiting) {
            monitor->state = DF_GVS_MONITOR_REQUESTING;
            monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
            monitor->next_action_ms =
                now_ms + DF_GVS_MONITOR_REQUEST_INTERVAL_MS;
            monitor->first_frame_deadline_ms = 0U;
            monitor->status_retry_deadline_ms = 0U;
            monitor->media_ready = false;
            monitor->status_retry_pending = false;
            monitor->retry_waiting = true;
            monitor->stop_sent = false;
        }
        result->retrying = true;
        return DF_OK;
    }
    if (monitor->state == DF_GVS_MONITOR_STOPPING && frame->opcode == 0x82U &&
        frame->payload_length == 0U) {
        monitor->last_now_ms = now_ms;
        monitor->state = DF_GVS_MONITOR_IDLE;
        monitor->failure = DF_GVS_MONITOR_FAILURE_NONE;
        monitor->media_ready = false;
        result->stopped = true;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

int df_gvs_monitor_admit_jpeg(struct df_gvs_monitor *monitor,
    const uint8_t source[6], const uint8_t destination[6],
    uint32_t source_ipv4, uint64_t generation, uint64_t now_ms,
    struct df_gvs_monitor_result *result) {
    if (result == NULL) return DF_ERR_INVALID;
    memset(result, 0, sizeof(*result));
    if (monitor == NULL || source == NULL || destination == NULL) {
        return DF_ERR_INVALID;
    }
    if (now_ms < monitor->last_now_ms)
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_CLOCK;
    else if (generation == 0U || generation != monitor->generation)
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_GENERATION;
    else if (source_ipv4 != monitor->station_ipv4)
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_SOURCE_IPV4;
    else if ((monitor->state == DF_GVS_MONITOR_REQUESTING &&
              monitor->retry_waiting) ||
             (monitor->state != DF_GVS_MONITOR_REQUESTING &&
             monitor->state != DF_GVS_MONITOR_AWAITING_VIDEO &&
             monitor->state != DF_GVS_MONITOR_PUBLISHING &&
             monitor->state != DF_GVS_MONITOR_VIEWING))
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_STATE;
    else if (memcmp(source, monitor->station, sizeof(monitor->station)) != 0)
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_SOURCE;
    else if (memcmp(destination, monitor->local, sizeof(monitor->local)) != 0)
        result->admit_reject = DF_GVS_MONITOR_ADMIT_REJECT_DESTINATION;
    if (result->admit_reject != DF_GVS_MONITOR_ADMIT_REJECT_NONE)
        return DF_ERR_INVALID;
    monitor->last_now_ms = now_ms;
    monitor->status_retry_pending = false;
    monitor->status_retry_deadline_ms = 0U;
    if (monitor->state == DF_GVS_MONITOR_REQUESTING) {
        monitor->state = DF_GVS_MONITOR_AWAITING_VIDEO;
        monitor->next_action_ms = 0U;
    }
    if (!monitor->media_ready) {
        monitor->media_ready = true;
        result->media_ready = true;
    }
    return DF_OK;
}

int df_gvs_monitor_mark_publishing(struct df_gvs_monitor *monitor,
    uint64_t generation, uint64_t now_ms) {
    if (monitor == NULL || generation == 0U || generation != monitor->generation ||
        now_ms < monitor->last_now_ms || !monitor->media_ready ||
        monitor->state != DF_GVS_MONITOR_AWAITING_VIDEO) {
        return DF_ERR_INVALID;
    }
    monitor->last_now_ms = now_ms;
    monitor->state = DF_GVS_MONITOR_PUBLISHING;
    return DF_OK;
}

int df_gvs_monitor_set_viewing(struct df_gvs_monitor *monitor,
    uint64_t generation, bool active, uint64_t now_ms) {
    if (monitor == NULL || generation == 0U || generation != monitor->generation ||
        now_ms < monitor->last_now_ms ||
        (monitor->state != DF_GVS_MONITOR_PUBLISHING &&
         monitor->state != DF_GVS_MONITOR_VIEWING)) {
        return DF_ERR_INVALID;
    }
    monitor->last_now_ms = now_ms;
    monitor->state = active ? DF_GVS_MONITOR_VIEWING : DF_GVS_MONITOR_PUBLISHING;
    return DF_OK;
}

int df_gvs_monitor_stop(struct df_gvs_monitor *monitor, uint64_t generation,
    uint64_t now_ms) {
    if (monitor == NULL || generation == 0U || generation != monitor->generation ||
        now_ms < monitor->last_now_ms || !df_gvs_monitor_active(monitor->state)) {
        return DF_ERR_INVALID;
    }
    monitor->last_now_ms = now_ms;
    monitor->state = DF_GVS_MONITOR_STOPPING;
    monitor->next_action_ms = now_ms;
    monitor->status_retry_deadline_ms = 0U;
    monitor->status_retry_pending = false;
    monitor->retry_waiting = false;
    monitor->stop_sent = false;
    return DF_OK;
}
