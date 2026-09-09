#include "runtime_service.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "capture.h"
#include "capture_retry.h"
#include "event.h"
#include "gvs_deadline.h"
#include "gvs_identity.h"
#include "gvs_memory_sender.h"
#include "gvs_call_runtime.h"
#include "gvs_packet.h"
#include "gvs_receive.h"
#include "gvs_reply_queue.h"
#include "gvs_runtime_sync.h"
#include "gvs_send_transaction.h"
#include "gvs_sync_state.h"
#include "runtime_ubus.h"

#define DF_RUNTIME_IDLE_POLL_MS 50U

static volatile sig_atomic_t df_runtime_stopping = 0;

static void df_runtime_stop(int signal_number) {
    (void)signal_number;
    df_runtime_stopping = 1;
}

static uint64_t df_monotonic_ms(void) {
    struct timespec now = {0};

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void df_log_transition(const struct df_gvs_transition_event *event) {
    (void)printf("doorfast: event=%s generation=%llu\n",
                 df_event_type_name(event->type),
                 (unsigned long long)event->generation);
}

static const char *df_sync_action_name(enum df_gvs_presence_action_type type) {
    switch (type) {
    case DF_GVS_PRESENCE_PEER_PROBE: return "peer_probe";
    case DF_GVS_PRESENCE_PEER_ONLINE: return "peer_online";
    case DF_GVS_PRESENCE_PEER_OFFLINE: return "peer_offline";
    case DF_GVS_PRESENCE_SYNC_ASK_ACTION: return "sync_ask";
    case DF_GVS_PRESENCE_SYNC_VERSION_ASK: return "version_ask";
    case DF_GVS_PRESENCE_PERIODIC_SYNC: return "periodic_sync";
    default: return "unknown";
    }
}

static int df_runtime_sync_action(
    const struct df_gvs_presence_action *action, void *context) {
    (void)context;
    if (action == NULL) {
        return DF_ERR_INVALID;
    }
    (void)printf("doorfast: event=sync_action action=%s round=%u mode=passive\n",
                 df_sync_action_name(action->type), action->round);
    return DF_OK;
}

static void df_runtime_sync_log(
    const struct df_gvs_runtime_sync *sync,
    const struct df_gvs_runtime_sync_result *result) {
    (void)printf(
        "doorfast: event=sync_observed opcode=%u accepted=%u rejected=%u "
        "version=%u maintainer=%u resend=%u mode=passive\n",
        (unsigned)result->opcode, result->accepted ? 1U : 0U,
        result->rejected ? 1U : 0U,
        (unsigned)sync->presence.sync_version,
        sync->presence.sync_maintainer ? 1U : 0U,
        result->resend_local ? 1U : 0U);
}

static const char *df_runtime_send_event_name(
    enum df_gvs_send_event_type type) {
    switch (type) {
    case DF_GVS_SEND_EVENT_SENDING: return "sending";
    case DF_GVS_SEND_EVENT_RETRY: return "retry";
    case DF_GVS_SEND_EVENT_SUCCESS: return "success";
    case DF_GVS_SEND_EVENT_FAILED: return "failed";
    case DF_GVS_SEND_EVENT_TIMEOUT: return "timeout";
    default: return "unknown";
    }
}

static void df_runtime_send_log(const struct df_gvs_send_trace *trace) {
    size_t index;

    for (index = 0; trace != NULL && index < trace->count; index++) {
        (void)printf(
            "doorfast: event=peer_reply_tx state=%s attempt=%u "
            "timed_out=%u mode=simulated\n",
            df_runtime_send_event_name(trace->events[index].type),
            trace->events[index].attempt,
            trace->events[index].timed_out ? 1U : 0U);
    }
}

static void df_runtime_memory_frame_log(
    const struct df_gvs_memory_frame_record *record) {
    if (record == NULL || !record->valid) {
        return;
    }
    (void)printf(
        "doorfast: event=peer_reply_frame prepared=1 length=%zu attempt=%u "
        "header=placeholder mode=memory\n",
        record->length, record->attempt);
}

static int df_runtime_capture_open(const struct df_runtime_config *runtime,
                                   struct df_capture **capture) {
    int status = df_capture_open(runtime->config.gvs_interface,
                                 runtime->config.capture_promiscuous, capture);

    if (status != DF_OK) {
        return status;
    }
    if (df_capture_set_filter(*capture, df_capture_default_filter()) != DF_OK) {
        df_capture_close(*capture);
        *capture = NULL;
        return DF_ERR_IO;
    }
    return DF_OK;
}

struct df_runtime_wait_context {
    struct df_runtime_ubus *ubus;
    bool ubus_started;
};

int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms,
                          df_runtime_delay_slice_fn run_slice,
                          void *context) {
    unsigned remaining = delay_ms;

    if (delay_ms == 0U || max_slice_ms == 0U || run_slice == NULL) {
        return DF_ERR_INVALID;
    }
    while (remaining > 0U) {
        unsigned slice = remaining < max_slice_ms ? remaining : max_slice_ms;

        if (run_slice(slice, context) != DF_OK) {
            return DF_ERR_IO;
        }
        remaining -= slice;
    }
    return DF_OK;
}

static int df_runtime_wait_and_pump(unsigned delay_ms, void *context) {
    struct df_runtime_wait_context *wait = context;
    struct timespec duration = {
        .tv_sec = (time_t)(delay_ms / 1000U),
        .tv_nsec = (long)(delay_ms % 1000U) * 1000000L,
    };

    (void)nanosleep(&duration, NULL);
    if (wait != NULL && wait->ubus_started &&
        df_runtime_ubus_process(wait->ubus, df_monotonic_ms()) != DF_OK) {
        (void)fputs("doorfast: event=ubus_process_failed\n", stderr);
    }
    return DF_OK;
}

static int df_runtime_status_provider(
    struct df_gvs_runtime_sync_status *status, void *context) {
    return df_gvs_runtime_sync_status(context, status);
}

int df_runtime_service_run(const struct df_runtime_config *runtime) {
    struct df_capture *capture = NULL;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_capture_retry retry = {0};
    struct df_gvs_reply_queue reply_queue = {0};
    struct df_gvs_send_transaction send_transaction = {0};
    struct df_gvs_memory_sender memory_sender = {0};
    struct df_gvs_call_ack call_ack = {0};
    struct df_gvs_runtime_sync sync = {0};
    struct df_runtime_ubus ubus = {0};
    struct df_runtime_wait_context wait_context = {
        .ubus = &ubus,
    };
    uint8_t identity[6];
    uint16_t persisted_version;
    uint64_t started_ms;
    uint64_t logged_frame_generation = 0;
    int status;

    if (runtime == NULL || df_config_validate(&runtime->config) != DF_OK ||
        !runtime->config.enabled ||
        df_gvs_identity_parse(runtime->config.gvs_local_address, identity) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_sync_state_load(runtime->config.sync_state_path,
                               &persisted_version) != DF_OK) {
        (void)fprintf(stderr,
                      "doorfast: invalid or unreadable sync state: %s\n",
                      runtime->config.sync_state_path);
        return DF_ERR_IO;
    }
    started_ms = df_monotonic_ms();
    df_gvs_call_ack_init(&call_ack, started_ms);
    if (df_gvs_reply_queue_init(&reply_queue, started_ms) != DF_OK ||
        df_gvs_send_transaction_init(&send_transaction, started_ms) != DF_OK ||
        df_gvs_memory_sender_init(
            &memory_sender, identity, df_gvs_placeholder_header_fields,
            NULL) != DF_OK ||
        df_gvs_runtime_sync_start(&sync, identity, persisted_version,
                                  started_ms) != DF_OK) {
        return DF_ERR_IO;
    }
    if (df_runtime_capture_open(runtime, &capture) != DF_OK) {
        df_gvs_runtime_sync_stop(&sync);
        return DF_ERR_IO;
    }
    if (df_runtime_ubus_start(&ubus, df_runtime_status_provider, &sync,
                              started_ms) == DF_OK) {
        wait_context.ubus_started = true;
    } else {
        (void)fputs("doorfast: event=ubus_start_failed\n", stderr);
    }
    df_runtime_stopping = 0;
    if (signal(SIGINT, df_runtime_stop) == SIG_ERR ||
        signal(SIGTERM, df_runtime_stop) == SIG_ERR) {
        df_runtime_ubus_stop(&ubus);
        df_capture_close(capture);
        return DF_ERR_IO;
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)printf("doorfast: observing interface=%s mode=passive\n",
                 runtime->config.gvs_interface);
    while (!df_runtime_stopping) {
        const uint8_t *packet = NULL;
        const uint8_t *payload = NULL;
        size_t packet_length = 0;
        size_t payload_length = 0;
        uint64_t now_ms;
        bool timed_out = false;
        size_t expired_replies = 0;
        struct df_gvs_send_trace send_trace;
        int captured = df_capture_next(capture, &packet, &packet_length);

        now_ms = df_monotonic_ms();
        if (df_gvs_reply_queue_expire(&reply_queue, now_ms,
                                      &expired_replies) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (expired_replies > 0U) {
            (void)printf(
                "doorfast: event=peer_reply_expired count=%zu pending=%zu "
                "mode=passive\n",
                expired_replies, df_gvs_reply_queue_count(&reply_queue));
        }
        if (df_gvs_send_transaction_step(
                &send_transaction, &reply_queue, now_ms,
                df_gvs_memory_send_attempt, &memory_sender,
                &send_trace) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (memory_sender.record.generation != logged_frame_generation) {
            df_runtime_memory_frame_log(&memory_sender.record);
            logged_frame_generation = memory_sender.record.generation;
        }
        df_runtime_send_log(&send_trace);
        if (df_gvs_runtime_sync_tick(&sync, now_ms, df_runtime_sync_action,
                                     NULL) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (df_gvs_deadline_tick(&deadline, &session, now_ms, &timed_out) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        {
            enum df_gvs_call_ack_state previous = call_ack.state;
            if (df_gvs_call_ack_tick(&call_ack, &session, identity, now_ms) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            if (previous == DF_GVS_CALL_ACK_WAITING &&
                call_ack.state == DF_GVS_CALL_ACK_EXPIRED) {
                (void)fputs("doorfast: event=call_ack_expired mode=passive\n", stdout);
            } else if (previous == DF_GVS_CALL_ACK_WAITING &&
                       call_ack.state == DF_GVS_CALL_ACK_CANCELLED) {
                (void)fputs("doorfast: event=call_ack_cancelled mode=passive\n", stdout);
            }
        }
        if (wait_context.ubus_started &&
            df_runtime_ubus_process(&ubus, now_ms) != DF_OK) {
            (void)fputs("doorfast: event=ubus_process_failed\n", stderr);
        }
        if (timed_out) {
            (void)printf("doorfast: event=session_timeout generation=%llu\n",
                         (unsigned long long)session.generation);
        }

        if (captured == DF_CAPTURE_TIMEOUT) {
            if (df_runtime_pump_delay(DF_RUNTIME_IDLE_POLL_MS,
                                      DF_RUNTIME_IDLE_POLL_MS,
                                      df_runtime_wait_and_pump,
                                      &wait_context) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            continue;
        }
        if (captured == DF_CAPTURE_ERROR) {
            bool ended = false;

            (void)df_gvs_session_abort(&session, &ended);
            df_gvs_deadline_cancel(&deadline);
            if (df_gvs_call_ack_tick(&call_ack, &session, identity, now_ms) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            if (ended) {
                (void)printf("doorfast: event=network_lost generation=%llu\n",
                             (unsigned long long)session.generation);
            }
            df_capture_close(capture);
            capture = NULL;
            df_gvs_runtime_sync_stop(&sync);
            while (!df_runtime_stopping) {
                unsigned delay_ms;

                if (df_capture_retry_next(&retry, &delay_ms) != DF_OK) {
                    status = DF_ERR_IO;
                    goto done;
                }
                (void)printf("doorfast: capture_retry=%u delay_ms=%u interface=%s\n",
                             retry.attempts, delay_ms, runtime->config.gvs_interface);
                if (df_runtime_pump_delay(delay_ms, 250U,
                                          df_runtime_wait_and_pump,
                                          &wait_context) != DF_OK) {
                    status = DF_ERR_IO;
                    goto done;
                }
                if (df_runtime_stopping) {
                    break;
                }
                if (df_runtime_capture_open(runtime, &capture) == DF_OK) {
                    if (df_gvs_runtime_sync_restart(&sync,
                                                    df_monotonic_ms()) != DF_OK) {
                        df_capture_close(capture);
                        capture = NULL;
                        status = DF_ERR_IO;
                        goto done;
                    }
                    df_capture_retry_reset(&retry);
                    (void)printf("doorfast: capture_recovered interface=%s\n",
                                 runtime->config.gvs_interface);
                    break;
                }
            }
            if (df_runtime_stopping) {
                status = DF_OK;
                goto done;
            }
            continue;
        }
        if (df_gvs_extract_control_payload(packet, packet_length,
                                           &payload, &payload_length) == 1) {
            if (payload_length >= 40U && payload[38] == 0x07 &&
                payload[39] == 0x01) {
                struct df_gvs_peer_reply reply;
                bool coalesced = false;
                int peer_status = df_gvs_presence_receive_peer_request(
                    &sync.presence, payload, payload_length, now_ms, &reply,
                    df_runtime_sync_action, NULL);
                int queue_status = peer_status == DF_OK
                    ? df_gvs_reply_queue_enqueue(&reply_queue, &reply, now_ms,
                                                 &coalesced)
                    : DF_ERR_INVALID;
                (void)printf(
                    "doorfast: event=peer_probe accepted=%u reply_pending=%u "
                    "peer_observed=%u mode=passive pending=%zu coalesced=%u "
                    "queue_full=%u\n",
                    peer_status == DF_OK ? 1U : 0U,
                    queue_status == DF_OK ? 1U : 0U,
                    peer_status == DF_OK && reply.peer_observed ? 1U : 0U,
                    df_gvs_reply_queue_count(&reply_queue),
                    coalesced ? 1U : 0U,
                    peer_status == DF_OK && queue_status == DF_ERR_IO ? 1U : 0U);
                continue;
            }
            if (payload_length >= 40U && payload[38] == 0x07 &&
                payload[39] == 0x81) {
                int peer_status = df_gvs_presence_receive_peer(
                    &sync.presence, payload, payload_length, now_ms,
                    df_runtime_sync_action, NULL);
                (void)printf("doorfast: event=peer_reply accepted=%u mode=passive\n",
                             peer_status == DF_OK ? 1U : 0U);
                continue;
            }
            struct df_gvs_runtime_sync_result sync_result;
            struct df_gvs_call_runtime_result call_result;
            if (df_gvs_runtime_sync_receive(&sync, payload, payload_length,
                                            now_ms, &sync_result) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            if (sync_result.handled) {
                df_runtime_sync_log(&sync, &sync_result);
                if (sync_result.version_changed &&
                    df_gvs_sync_state_save(runtime->config.sync_state_path,
                                           sync.presence.sync_version) != DF_OK) {
                    (void)fprintf(stderr,
                                  "doorfast: event=sync_state_save_failed path=%s\n",
                                  runtime->config.sync_state_path);
                }
                continue;
            }
            if (df_gvs_call_runtime_receive(&call_ack, payload, payload_length,
                                            identity, &session, &deadline,
                                            now_ms, &call_result) == DF_OK) {
                const struct df_gvs_receive_result *result = &call_result.receive;
                unsigned i;
                if (call_result.acknowledgement_confirmed) {
                    (void)fputs("doorfast: event=call_ack_confirmed mode=passive\n", stdout);
                } else if (call_result.acknowledgement_rejected) {
                    (void)fputs("doorfast: event=call_ack_rejected mode=passive\n", stdout);
                }
                if (call_result.acknowledgement_expired) {
                    (void)fputs("doorfast: event=call_ack_expired mode=passive\n", stdout);
                } else if (call_result.acknowledgement_cancelled) {
                    (void)fputs("doorfast: event=call_ack_cancelled mode=passive\n", stdout);
                }
                for (i = 0; i < result->transition.count; ++i) {
                    df_log_transition(&result->transition.events[i]);
                }
                if (result->talking_transition) {
                    (void)printf("doorfast: event=session_established generation=%llu\n",
                                 (unsigned long long)session.generation);
                }
                if (result->observed_hangup) {
                    (void)printf("doorfast: event=hangup generation=%llu\n",
                                 (unsigned long long)session.generation);
                }
                if (result->timed_out_transition) {
                    (void)printf("doorfast: event=session_timeout generation=%llu\n",
                                 (unsigned long long)session.generation);
                }
            }
        }
    }
    status = DF_OK;

done:
    df_runtime_ubus_stop(&ubus);
    df_gvs_runtime_sync_stop(&sync);
    df_capture_close(capture);
    (void)fputs("doorfast: stopped\n", stdout);
    return status;
}
