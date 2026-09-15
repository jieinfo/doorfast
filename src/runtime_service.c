#include "runtime_service.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "capture.h"
#include "capture_retry.h"
#include "event.h"
#include "event_stream.h"
#include "gvs_deadline.h"
#include "gvs_identity.h"
#include "gvs_incoming_reply.h"
#include "gvs_memory_sender.h"
#include "gvs_call_control.h"
#include "gvs_packet.h"
#include "gvs_receive.h"
#include "gvs_reply_queue.h"
#include "gvs_runtime_sync.h"
#include "gvs_send_transaction.h"
#include "gvs_udp_sender.h"
#include "gvs_elevator_query.h"
#include "gvs_media.h"
#include "gvs_media_admission.h"
#include "gvs_media_lifecycle.h"
#include "gvs_audio_buffer.h"
#include "gvs_audio_chunk_store.h"
#include "gvs_audio_tx.h"
#include "gvs_pcm_ingress.h"
#include "gvs_pcm_pump.h"
#include "g711_alaw.h"
#include "gvs_video_frame_cache.h"
#include "gvs_video_reassembly.h"
#include "gvs_video_snapshot.h"
#include "gvs_vendor_header.h"
#include "gvs_transport_policy.h"
#include "gvs_sync_state.h"
#include "runtime_ubus.h"

#define DF_RUNTIME_IDLE_POLL_MS 10U
#define DF_RUNTIME_AUDIO_SNAPSHOT "/tmp/doorfast-latest.wav"
#define DF_RUNTIME_AUDIO_CHUNKS "/tmp/doorfast-audio-chunks"
#define DF_RUNTIME_VIDEO_SNAPSHOT "/tmp/doorfast-latest.jpg"

static volatile sig_atomic_t df_runtime_stopping = 0;
static uint8_t df_runtime_audio_export[DF_GVS_AUDIO_BUFFER_CAPACITY];

static void df_runtime_media_clear(struct df_gvs_video_reassembly *video,
                                   struct df_gvs_video_frame_cache *video_cache,
                                   struct df_gvs_audio_buffer *audio,
                                   uint64_t generation)
{
    df_gvs_video_reassembly_reset(video);
    df_gvs_video_frame_cache_reset(video_cache);
    df_gvs_audio_buffer_reset(audio, generation);
    (void)remove(DF_RUNTIME_AUDIO_SNAPSHOT);
    (void)df_gvs_audio_chunk_store_clear(DF_RUNTIME_AUDIO_CHUNKS);
    (void)remove(DF_RUNTIME_VIDEO_SNAPSHOT);
}

static int df_runtime_media_sync(
    struct df_gvs_media_lifecycle *lifecycle,
    const struct df_gvs_session *session,
    struct df_gvs_video_reassembly *video,
    struct df_gvs_video_frame_cache *video_cache,
    struct df_gvs_audio_buffer *audio)
{
    struct df_gvs_media_lifecycle_result result;

    if (df_gvs_media_lifecycle_sync(lifecycle, session, &result) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (result.clear) {
        df_runtime_media_clear(video, video_cache, audio, result.generation);
    }
    return DF_OK;
}

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

static const char *df_runtime_handshake_log_mode(
    const struct df_runtime_config *runtime) {
    if (runtime != NULL &&
        (!runtime->config.passive_only || runtime->config.active_host)) {
        return "active_host";
    }
    return "simulated";
}

static int df_runtime_sync_action(
    const struct df_gvs_presence_action *action, void *context) {
    struct df_gvs_udp_presence_context *udp = context;
    if (action == NULL) {
        return DF_ERR_INVALID;
    }
    if (udp != NULL) {
        if (df_gvs_udp_presence_emit(action, udp) != DF_OK) {
            (void)printf("doorfast: event=sync_action action=%s round=%u sent=0\n",
                         df_sync_action_name(action->type), action->round);
            return DF_ERR_IO;
        }
    }
    (void)printf("doorfast: event=sync_action action=%s round=%u sent=%u\n",
                 df_sync_action_name(action->type), action->round,
                 udp != NULL ? 1U : 0U);
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
    struct df_event_stream *event_stream;
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
    if (wait != NULL && wait->event_stream != NULL) {
        (void)df_event_stream_process(wait->event_stream);
    }
    return DF_OK;
}

static void df_runtime_publish_event(struct df_event_stream *stream,
                                     const char *event, uint64_t generation,
                                     uint64_t now_ms) {
    if (stream != NULL && stream->listen_fd >= 0 && df_event_stream_publish(
            stream, event, generation, now_ms) != DF_OK) {
        (void)fprintf(stderr, "doorfast: event_stream_publish_failed event=%s\n",
                      event != NULL ? event : "unknown");
    }
}

static int df_runtime_status_provider(
    struct df_gvs_runtime_sync_status *status, void *context) {
    return df_gvs_runtime_sync_status(context, status);
}

struct df_runtime_call_binding {
    struct df_gvs_call_control *control;
    struct df_gvs_session *session;
    const uint8_t *identity;
};

static int df_runtime_call_status_provider(
    struct df_gvs_call_control_status *status, void *context) {
    struct df_runtime_call_binding *binding = context;

    if (binding == NULL) {
        return DF_ERR_INVALID;
    }
    return df_gvs_call_control_status(
        binding->control, binding->session, status);
}

static int df_runtime_call_submit(
    const struct df_runtime_call_request *request, uint64_t now_ms,
    void *context) {
    struct df_runtime_call_binding *binding = context;

    if (request == NULL || binding == NULL) {
        return DF_ERR_INVALID;
    }
    if (request->type == DF_GVS_CALL_COMMAND_ANSWER) {
        return df_gvs_call_control_submit_answer(
            binding->control, binding->session, request->session_generation,
            binding->identity, request->primary_media_port,
            request->secondary_media_port, request->duration_seconds, now_ms);
    }
    if (request->type == DF_GVS_CALL_COMMAND_HANGUP) {
        return df_gvs_call_control_submit_hangup(
            binding->control, binding->session, request->session_generation,
            binding->identity, request->reason, now_ms);
    }
    return DF_ERR_INVALID;
}

int df_runtime_service_run(const struct df_runtime_config *runtime) {
    struct df_capture *capture = NULL;
    struct df_gvs_session session = {0};
    struct df_gvs_deadline deadline = {0};
    struct df_capture_retry retry = {0};
    struct df_gvs_reply_queue reply_queue = {0};
    struct df_gvs_send_transaction send_transaction = {0};
    struct df_gvs_memory_sender memory_sender = {0};
    struct df_gvs_call_control call_control = {0};
    struct df_gvs_access_control access = {0};
    struct df_gvs_elevator_control elevator = {0};
    struct df_gvs_elevator_query elevator_query = {0};
    struct df_gvs_udp_sender udp_sender = {.fd = -1};
    struct df_gvs_udp_presence_context presence_context = {0};
    struct df_gvs_runtime_sync sync = {0};
    struct df_gvs_video_reassembly video = {0};
    struct df_gvs_video_frame_cache video_cache = {0};
    struct df_gvs_media_lifecycle media_lifecycle = {0};
    struct df_gvs_audio_buffer audio = {0};
    struct df_gvs_audio_tx audio_tx = {0};
    struct df_gvs_pcm_ingress pcm_ingress = {.fd = -1};
    struct df_runtime_ubus ubus = {0};
    struct df_event_stream event_stream = {.listen_fd = -1};
    struct df_runtime_wait_context wait_context = {
        .ubus = &ubus,
        .event_stream = &event_stream,
    };
    uint8_t identity[6];
    struct df_runtime_call_binding call_binding = {
        .control = &call_control,
        .session = &session,
        .identity = identity,
    };
    uint16_t persisted_version;
    uint64_t started_ms;
    uint64_t logged_frame_generation = 0;
    uint64_t auto_elevator_generation = 0;
    uint64_t last_audio_export_ms = 0;
    enum df_gvs_elevator_control_state logged_elevator_state =
        DF_GVS_ELEVATOR_CONTROL_IDLE;
    int status;

    if (runtime == NULL || df_config_validate(&runtime->config) != DF_OK ||
        !runtime->config.enabled ||
        df_gvs_identity_parse(runtime->config.gvs_local_address, identity) != DF_OK) {
        return DF_ERR_INVALID;
    }
    {
        const struct df_gvs_transport_policy policy = {
            .passive_only = runtime->config.passive_only &&
                !runtime->config.active_host,
            .real_send_requested = runtime->config.active_host,
            .one_shot = runtime->config.active_host,
            .rollback_ready = runtime->config.active_host,
        };
        if (df_gvs_transport_policy_validate(&policy) != DF_OK)
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
    df_gvs_video_reassembly_init(&video);
    df_gvs_video_frame_cache_init(&video_cache);
    df_gvs_media_lifecycle_init(&media_lifecycle);
    df_gvs_audio_buffer_init(&audio);
    if (df_runtime_media_sync(
            &media_lifecycle, &session, &video, &video_cache,
            &audio) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (df_gvs_audio_tx_init(&audio_tx, (uint16_t)started_ms,
            df_gvs_udp_audio_emit, &udp_sender) != DF_OK)
        return DF_ERR_INVALID;
    if (df_gvs_access_control_init(&access, runtime->config.access_material,
            started_ms, df_gvs_udp_access_emit, &udp_sender) != DF_OK)
        return DF_ERR_INVALID;
    if (df_gvs_elevator_control_init(&elevator, started_ms,
            df_gvs_udp_elevator_emit, &udp_sender) != DF_OK)
        return DF_ERR_INVALID;
    if (df_gvs_reply_queue_init(&reply_queue, started_ms) != DF_OK ||
        df_gvs_send_transaction_init(&send_transaction, started_ms) != DF_OK ||
        df_gvs_memory_sender_init(
            &memory_sender, identity, df_gvs_placeholder_header_fields,
            NULL) != DF_OK ||
        df_gvs_call_control_init(
            &call_control, started_ms, df_gvs_placeholder_header_fields,
            NULL) != DF_OK ||
        df_gvs_runtime_sync_start(&sync, identity, persisted_version,
                                  started_ms) != DF_OK) {
        return DF_ERR_IO;
    }
    if (!runtime->config.passive_only || runtime->config.active_host) {
        if (df_gvs_udp_sender_open(&udp_sender, runtime->config.indoor_ipaddr, 8300,
                df_gvs_vendor_header_fields, NULL) != DF_OK) {
            return DF_ERR_IO;
        }
        df_gvs_call_control_set_sender(&call_control,
            df_gvs_udp_send_attempt, &udp_sender);
        df_gvs_call_control_set_handshake_sender(&call_control,
            df_gvs_udp_send_attempt, &udp_sender);
        presence_context.sender = &udp_sender;
        presence_context.source = identity;
    }
    if (df_gvs_elevator_query_init(&elevator_query, identity,
            runtime->config.active_host, started_ms,
            df_gvs_udp_elevator_emit, &udp_sender) != DF_OK) {
        df_gvs_udp_sender_close(&udp_sender);
        df_gvs_runtime_sync_stop(&sync);
        return DF_ERR_INVALID;
    }
    if (df_runtime_capture_open(runtime, &capture) != DF_OK) {
        df_gvs_udp_sender_close(&udp_sender);
        df_gvs_runtime_sync_stop(&sync);
        return DF_ERR_IO;
    }
    if ((!runtime->config.passive_only || runtime->config.active_host) &&
        df_gvs_pcm_ingress_open(
            &pcm_ingress, DF_GVS_PCM_INGRESS_DEFAULT_PATH) != DF_OK) {
        df_capture_close(capture);
        df_gvs_udp_sender_close(&udp_sender);
        df_gvs_runtime_sync_stop(&sync);
        return DF_ERR_IO;
    }
    if (df_runtime_ubus_start(&ubus, df_runtime_status_provider, &sync,
                              started_ms) == DF_OK &&
        df_runtime_ubus_bind_call(
            &ubus, df_runtime_call_status_provider, df_runtime_call_submit,
            &call_binding) == DF_OK &&
        df_runtime_ubus_bind_access(&ubus, &access, &session, identity) == DF_OK &&
        df_runtime_ubus_bind_elevator(&ubus, &elevator, identity) == DF_OK &&
        df_runtime_ubus_bind_audio(&ubus, &audio) == DF_OK &&
        df_runtime_ubus_bind_audio_tx(&ubus, &audio_tx) == DF_OK &&
        df_runtime_ubus_bind_video(&ubus, &video_cache) == DF_OK) {
        wait_context.ubus_started = true;
        df_runtime_ubus_set_active_host(&ubus,
            !runtime->config.passive_only || runtime->config.active_host);
    } else {
        df_runtime_ubus_stop(&ubus);
        (void)fputs("doorfast: event=ubus_start_failed\n", stderr);
    }
    if (df_event_stream_init(&event_stream, DF_EVENT_STREAM_DEFAULT_PATH) != DF_OK) {
        (void)fputs("doorfast: event_stream_disabled\n", stderr);
    }
    df_runtime_stopping = 0;
    if (signal(SIGINT, df_runtime_stop) == SIG_ERR ||
        signal(SIGTERM, df_runtime_stop) == SIG_ERR) {
        df_runtime_ubus_stop(&ubus);
        df_event_stream_stop(&event_stream);
        df_gvs_pcm_ingress_close(&pcm_ingress);
        df_capture_close(capture);
        df_gvs_udp_sender_close(&udp_sender);
        df_gvs_runtime_sync_stop(&sync);
        return DF_ERR_IO;
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)printf("doorfast: observing interface=%s mode=%s\n",
                 runtime->config.gvs_interface,
                 (runtime->config.passive_only && !runtime->config.active_host)
                     ? "passive" : "active_host");
    while (!df_runtime_stopping) {
        struct df_capture_record capture_record;
        const uint8_t *packet = NULL;
        const uint8_t *payload = NULL;
        size_t packet_length = 0;
        size_t payload_length = 0;
        uint64_t now_ms;
        bool timed_out = false;
        size_t expired_replies = 0;
        struct df_gvs_send_trace send_trace;
        int captured = df_capture_next_record(capture, &capture_record);

        if (captured == DF_CAPTURE_PACKET) {
            packet = capture_record.data;
            packet_length = capture_record.captured_length;
        }

        now_ms = df_monotonic_ms();
        if (wait_context.ubus_started &&
            df_runtime_ubus_process(&ubus, now_ms) != DF_OK) {
            (void)fputs("doorfast: event=ubus_process_failed\n", stderr);
        }
        (void)df_event_stream_process(&event_stream);
        (void)df_gvs_access_result_tick(&access.result, &session, identity, now_ms);
        if (df_gvs_elevator_control_tick(&elevator, identity, now_ms) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (df_gvs_elevator_query_tick(&elevator_query, now_ms) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (elevator.state != logged_elevator_state) {
            (void)printf("doorfast: event=elevator_control state=%s "
                         "transaction_id=%llu attempts=%u successful_sends=%u "
                         "physical_result_confirmed=%u mode=%s\n",
                         df_gvs_elevator_control_state_name(elevator.state),
                         (unsigned long long)elevator.transaction_id,
                         elevator.attempts, elevator.successful_sends,
                         elevator.physical_result_confirmed ? 1U : 0U,
                         runtime->config.active_host ? "active_host" : "passive");
            logged_elevator_state = elevator.state;
        }
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
        presence_context.sync_version = sync.presence.sync_version;
        if (df_gvs_runtime_sync_tick(&sync, now_ms, df_runtime_sync_action,
                                     runtime->config.passive_only &&
                                     !runtime->config.active_host ? NULL :
                                     &presence_context) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        {
            struct df_gvs_call_control_result tick_result;
            if (df_gvs_call_control_step(&call_control, &session, identity,
                                         &deadline, now_ms, &tick_result) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            timed_out = tick_result.runtime.session_timed_out;
            if (tick_result.handshake_frame_ready)
                (void)fprintf(stdout,
                    "doorfast: event=handshake_frame opcode=%02x mode=%s "
                    "transport=%s\n",
                    call_control.handshake_dispatch.command.opcode,
                    df_runtime_handshake_log_mode(runtime),
                    (!runtime->config.passive_only || runtime->config.active_host) ?
                        "udp" : "memory");
            if (tick_result.handshake.disconnected)
                (void)fprintf(stdout,
                    "doorfast: event=handshake_disconnected mode=%s\n",
                    df_runtime_handshake_log_mode(runtime));
            if (tick_result.handshake_action_dropped)
                (void)fprintf(stdout,
                    "doorfast: event=handshake_action_dropped mode=%s\n",
                    df_runtime_handshake_log_mode(runtime));
            if (tick_result.runtime.acknowledgement_expired) {
                (void)fputs("doorfast: event=call_ack_expired mode=passive\n", stdout);
            } else if (tick_result.runtime.acknowledgement_cancelled) {
                (void)fputs("doorfast: event=call_ack_cancelled mode=passive\n", stdout);
            }
        }
        if ((!runtime->config.passive_only || runtime->config.active_host) &&
            df_gvs_audio_tx_sync(
                &audio_tx, &session, identity, now_ms) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (df_runtime_media_sync(
                &media_lifecycle, &session, &video, &video_cache,
                &audio) != DF_OK) {
            status = DF_ERR_IO;
            goto done;
        }
        if (!runtime->config.passive_only || runtime->config.active_host) {
            struct df_gvs_pcm_pump_result pcm_result;

            if (df_gvs_pcm_pump(
                    &pcm_ingress, &audio_tx, now_ms, &pcm_result) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            if (pcm_result.send_failed) {
                (void)fputs(
                    "doorfast: event=audio_tx_failed source=local_pcm\n",
                    stderr);
            }
        }
        if (timed_out) {
            (void)printf("doorfast: event=session_timeout generation=%llu\n",
                         (unsigned long long)session.generation);
            df_runtime_publish_event(&event_stream, "timeout",
                                     session.generation, now_ms);
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
            {
                struct df_gvs_call_control_result call_result;
                if (df_gvs_call_control_step(
                        &call_control, &session, identity, &deadline,
                        now_ms, &call_result) != DF_OK) {
                    status = DF_ERR_IO;
                    goto done;
                }
            }
            if ((!runtime->config.passive_only || runtime->config.active_host) &&
                df_gvs_audio_tx_sync(
                    &audio_tx, &session, identity, now_ms) != DF_OK) {
                status = DF_ERR_IO;
                goto done;
            }
            if (df_runtime_media_sync(
                    &media_lifecycle, &session, &video, &video_cache,
                    &audio) != DF_OK) {
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
        {
            struct df_udp_prefix media_prefix;
            int media_status = df_gvs_inspect_udp_prefix(
                packet, packet_length, &media_prefix);
            if (media_status == 1 && media_prefix.payload_complete &&
                (media_prefix.source_port == 8302 ||
                 media_prefix.destination_port == 8302) &&
                session.state != DF_GVS_IDLE && session.state != DF_GVS_ENDED) {
                struct df_gvs_audio_packet audio_packet;
                const uint8_t *media_payload = packet + media_prefix.payload_offset;
                if (df_gvs_parse_audio(media_payload,
                        media_prefix.declared_payload_length,
                        &audio_packet) == 0 &&
                    df_gvs_media_admit(&session, identity,
                        audio_packet.destination,
                        audio_packet.source) == DF_GVS_MEDIA_ACCEPTED &&
                    df_gvs_audio_buffer_push(&audio, audio_packet.payload,
                        audio_packet.payload_length, audio_packet.sequence,
                        session.generation, now_ms) == 0) {
                    (void)printf("doorfast: event=audio_frame generation=%llu bytes=%zu sequence=%u\n",
                        (unsigned long long)session.generation,
                        audio_packet.payload_length,
                        (unsigned)audio_packet.sequence);
                    if (audio.length >= 8000U &&
                        now_ms >= last_audio_export_ms + 1000U) {
                        size_t export_length = 0;
                        uint64_t previous_revision =
                            audio.snapshot_packet_count;
                        uint64_t revision = audio.packet_count;
                        uint64_t pending =
                            audio.byte_count - audio.snapshot_source_bytes;
                        if (df_gvs_audio_buffer_copy_pending(&audio,
                                df_runtime_audio_export,
                                sizeof(df_runtime_audio_export),
                                &export_length) == 0 &&
                            export_length > 0U) {
                            uint64_t dropped = pending > export_length
                                ? pending - export_length : 0U;
                            if (df_g711_alaw_write_wav(
                                    DF_RUNTIME_AUDIO_SNAPSHOT,
                                    df_runtime_audio_export,
                                    export_length) == 0 &&
                                df_gvs_audio_chunk_store_publish(
                                    DF_RUNTIME_AUDIO_CHUNKS,
                                    DF_RUNTIME_AUDIO_SNAPSHOT,
                                    session.generation, previous_revision,
                                    revision, 44U + export_length * 2U,
                                    dropped) == 0 &&
                                df_gvs_audio_buffer_mark_snapshot(
                                    &audio, session.generation,
                                    export_length, now_ms) == 0)
                                last_audio_export_ms = now_ms;
                        }
                    }
                }
                continue;
            }
            if (media_status == 1 && media_prefix.payload_complete &&
                (media_prefix.source_port == 8303 ||
                 media_prefix.destination_port == 8303) &&
                session.state != DF_GVS_IDLE && session.state != DF_GVS_ENDED) {
                struct df_gvs_video_packet video_packet;
                const uint8_t *frame = NULL;
                size_t frame_length = 0;
                const uint8_t *media_payload =
                    packet + media_prefix.payload_offset;
                if (df_gvs_parse_video(media_payload,
                        media_prefix.declared_payload_length,
                        &video_packet) == 0 &&
                    df_gvs_media_admit(&session, identity,
                        video_packet.destination,
                        video_packet.source) == DF_GVS_MEDIA_ACCEPTED) {
                    int frame_status = df_gvs_video_reassembly_push(
                        &video, &video_packet, &frame, &frame_length);
                    if (frame_status == DF_GVS_VIDEO_REASSEMBLY_COMPLETE &&
                        df_gvs_jpeg_validate(frame, frame_length) == 0) {
                        int cache_status = df_gvs_video_frame_cache_store(
                            &video_cache, frame, frame_length,
                            session.generation, video_packet.frame_no,
                            now_ms);
                        int snapshot_status = cache_status == 0
                            ? df_gvs_video_snapshot_write(
                                DF_RUNTIME_VIDEO_SNAPSHOT, frame, frame_length)
                            : -1;

                        if (cache_status == 0 && snapshot_status == 0) {
                            (void)printf(
                                "doorfast: event=video_frame generation=%llu "
                                "bytes=%zu frame=%u\n",
                                (unsigned long long)session.generation,
                                frame_length, (unsigned)video_packet.frame_no);
                        } else {
                            df_gvs_video_frame_cache_invalidate(&video_cache);
                            (void)remove(DF_RUNTIME_VIDEO_SNAPSHOT);
                        }
                    }
                }
                continue;
            }
        }
        if (df_gvs_extract_control_payload(packet, packet_length,
                                           &payload, &payload_length) == 1) {
            if ((!runtime->config.passive_only || runtime->config.active_host) &&
                payload_length >= DF_GVS_CONTROL_HEADER_SIZE)
                (void)df_gvs_udp_sender_observe_peer(&udp_sender, packet,
                    packet_length, identity);
            if (payload_length >= 40U && payload[38] == 0x07 &&
                payload[39] == 0x01) {
                struct df_gvs_peer_reply reply;
                bool coalesced = false;
                int peer_status = df_gvs_presence_receive_peer_request(
                    &sync.presence, payload, payload_length, now_ms, &reply,
                    df_runtime_sync_action,
                    runtime->config.passive_only && !runtime->config.active_host ?
                        NULL : &presence_context);
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
                    df_runtime_sync_action,
                    runtime->config.passive_only && !runtime->config.active_host ?
                        NULL : &presence_context);
                (void)printf("doorfast: event=peer_reply accepted=%u mode=passive\n",
                             peer_status == DF_OK ? 1U : 0U);
                continue;
            }
            struct df_gvs_runtime_sync_result sync_result;
            struct df_gvs_call_control_result call_result;
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
            {
                struct df_gvs_frame access_frame;
                struct df_event access_event;
                if (df_gvs_frame_parse(payload, payload_length, &access_frame,
                        &access_event) == DF_OK && access_frame.family == 0x04 &&
                    access_frame.opcode == 0x89) {
                    if (df_gvs_access_result_observe(&access.result, &session,
                            identity, &access_frame, now_ms) == DF_OK)
                        (void)fprintf(stdout, "doorfast: event=access_result state=%s raw_status=%u\n",
                            df_gvs_access_state_name(access.result.state),
                            (unsigned)access.result.raw_status);
                    continue;
                }
            }
            {
                struct df_gvs_frame elevator_frame;
                struct df_event elevator_event;
                if (df_gvs_frame_parse(payload, payload_length, &elevator_frame,
                        &elevator_event) == DF_OK && elevator_frame.family == 0x08) {
                    if (elevator_frame.opcode == 0x82) {
                        if (df_gvs_elevator_control_observe(&elevator, identity,
                                &elevator_frame, now_ms) == DF_OK)
                            (void)printf("doorfast: event=elevator_result state=protocol_completed "
                                         "transaction_id=%llu physical_result_confirmed=0\n",
                                         (unsigned long long)elevator.transaction_id);
                    } else if (elevator_frame.opcode == 0x83) {
                        struct df_gvs_elevator_status elevator_status;
                        if (df_gvs_elevator_parse_status(&elevator_frame, identity,
                                &elevator_status) == DF_OK &&
                            df_runtime_ubus_update_elevator_status(&ubus,
                                &elevator_status, now_ms) == DF_OK)
                            (void)printf("doorfast: event=elevator_status count=%zu "
                                         "status_valid=1\n", elevator_status.count);
                    }
                    continue;
                }
            }
            if (df_gvs_call_control_receive(
                    &call_control, payload, payload_length, identity,
                    &session, &deadline, now_ms, &call_result) == DF_OK) {
                const struct df_gvs_receive_result *result =
                    &call_result.runtime.receive;
                unsigned i;
                if (call_result.handshake.accepted_ask || call_result.handshake.accepted_reply)
                    (void)fprintf(stdout, "doorfast: event=handshake_received opcode=%02x mode=%s\n",
                        call_result.handshake.accepted_ask ? 0x51 : 0x52,
                        df_runtime_handshake_log_mode(runtime));
                if (call_result.handshake_action_dropped)
                    (void)fprintf(stdout,
                        "doorfast: event=handshake_action_dropped mode=%s\n",
                        df_runtime_handshake_log_mode(runtime));
                if (call_result.runtime.acknowledgement_confirmed) {
                    (void)fputs("doorfast: event=call_ack_confirmed mode=passive\n", stdout);
                } else if (call_result.runtime.acknowledgement_rejected) {
                    (void)fputs("doorfast: event=call_ack_rejected mode=passive\n", stdout);
                }
                if (call_result.runtime.acknowledgement_expired) {
                    (void)fputs("doorfast: event=call_ack_expired mode=passive\n", stdout);
                } else if (call_result.runtime.acknowledgement_cancelled) {
                    (void)fputs("doorfast: event=call_ack_cancelled mode=passive\n", stdout);
                }
                for (i = 0; i < result->transition.count; ++i) {
                    df_log_transition(&result->transition.events[i]);
                }
                if (result->preempted_session) {
                    if (result->transition.count > 0U) {
                        df_runtime_publish_event(&event_stream, "preempted",
                            result->transition.events[0].generation, now_ms);
                    }
                }
                if (result->accepted_call) {
                    df_runtime_publish_event(&event_stream, "incoming_call",
                        session.generation, now_ms);
                }
                if (runtime->config.active_host && result->accepted_call) {
                    struct df_gvs_incoming_reply incoming_reply;
                    int reply_status = df_gvs_incoming_reply_prepare(
                        result, &session, session.generation, identity, 8303,
                        &incoming_reply);

                    if (reply_status == DF_OK)
                        reply_status = df_gvs_udp_incoming_reply_emit(
                            &incoming_reply, &udp_sender);
                    (void)printf(
                        "doorfast: event=incoming_call_reply generation=%llu "
                        "sent=%u transport=udp\n",
                        (unsigned long long)session.generation,
                        reply_status == DF_OK ? 1U : 0U);
                }
                if (runtime->config.active_host && runtime->config.call_elev &&
                    result->accepted_call &&
                    session.generation != auto_elevator_generation) {
                    uint64_t transaction_id = 0;
                    if (df_runtime_ubus_call_elevator(&ubus,
                            DF_GVS_ELEVATOR_UP,
                            &transaction_id) == DF_OK) {
                        auto_elevator_generation = session.generation;
                        (void)printf(
                            "doorfast: event=automatic_elevator_call "
                            "generation=%llu transaction_id=%llu direction=%s\n",
                            (unsigned long long)session.generation,
                            (unsigned long long)transaction_id,
                                     "up");
                    } else {
                        (void)fputs(
                            "doorfast: event=automatic_elevator_call "
                            "submitted=0\n", stdout);
                    }
                }
                if (result->talking_transition) {
                    (void)printf("doorfast: event=session_established generation=%llu\n",
                                 (unsigned long long)session.generation);
                    df_runtime_publish_event(&event_stream, "call_established",
                        session.generation, now_ms);
                }
                if (result->observed_hangup) {
                    (void)printf("doorfast: event=hangup generation=%llu\n",
                                 (unsigned long long)session.generation);
                    df_runtime_publish_event(&event_stream, "hangup",
                        session.generation, now_ms);
                }
                if (result->timed_out_transition) {
                    (void)printf("doorfast: event=session_timeout generation=%llu\n",
                                 (unsigned long long)session.generation);
                    df_runtime_publish_event(&event_stream, "timeout",
                        session.generation, now_ms);
                }
                if ((!runtime->config.passive_only || runtime->config.active_host) &&
                    df_gvs_audio_tx_sync(
                        &audio_tx, &session, identity, now_ms) != DF_OK) {
                    status = DF_ERR_IO;
                    goto done;
                }
                if (df_runtime_media_sync(
                        &media_lifecycle, &session, &video, &video_cache,
                        &audio) != DF_OK) {
                    status = DF_ERR_IO;
                    goto done;
                }
            }
        }
    }
    status = DF_OK;

done:
    df_runtime_media_clear(&video, &video_cache, &audio, 0);
    df_gvs_pcm_ingress_close(&pcm_ingress);
    df_gvs_udp_sender_close(&udp_sender);
    df_event_stream_stop(&event_stream);
    df_runtime_ubus_stop(&ubus);
    df_gvs_runtime_sync_stop(&sync);
    df_capture_close(capture);
    (void)fputs("doorfast: stopped\n", stdout);
    return status;
}
