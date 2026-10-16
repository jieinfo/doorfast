#ifndef DOORFAST_RUNTIME_SERVICE_H
#define DOORFAST_RUNTIME_SERVICE_H

#include <stdint.h>

#include "gvs_call_control.h"
#include "gvs_runtime_sync.h"
#include "gvs_station_discovery.h"
#include "runtime_media_module.h"
#include "runtime_config.h"
#include "gvs_audio_playout.h"
#include "gvs_media_lifecycle.h"
#include "gvs_video_reassembly.h"
#include "gvs_video_frame_cache.h"
#include "access_credentials.h"

struct df_runtime_ubus;
struct df_event_stream;
struct df_runtime_call_request;

struct df_runtime_call_binding {
    /* Production coordinator authority; required for outbound admission. */
    struct df_runtime_ubus *ubus;
    struct df_gvs_call_control *control;
    struct df_gvs_session *session;
    struct df_gvs_deadline *deadline;
    const uint8_t *identity;
    const struct df_station_registry *stations;
};

int df_runtime_call_submit(const struct df_runtime_call_request *, uint64_t,
    void *);

struct df_runtime_media_paths {
    const char *audio_snapshot;
    const char *audio_chunks;
    const char *video_snapshot;
};

int df_runtime_audio_service(struct df_gvs_audio_jitter *,
    struct df_gvs_audio_playout *, uint64_t now_ms);
int df_runtime_receive_audio_payload(const uint8_t *, size_t,
    const struct df_gvs_session *, const uint8_t identity[6],
    struct df_gvs_audio_jitter *, uint64_t now_ms);
int df_runtime_media_sync(struct df_gvs_media_lifecycle *,
    const struct df_gvs_session *, struct df_gvs_video_reassembly *,
    struct df_gvs_video_frame_cache *, struct df_gvs_audio_jitter *,
    struct df_gvs_audio_playout *, enum df_gvs_session_state *previous_state,
    const struct df_runtime_media_paths *);

typedef int (*df_runtime_delay_slice_fn)(unsigned delay_ms, void *context);
typedef int (*df_runtime_station_scan_emit_fn)(
    const struct df_gvs_station_scan_action *, void *context);

int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms,
                          df_runtime_delay_slice_fn run_slice,
                          void *context);
int df_runtime_station_scan_tick(struct df_gvs_station_scan *, uint64_t now_ms,
    df_runtime_station_scan_emit_fn, void *context);
int df_runtime_station_scan_service(bool enabled,
    struct df_gvs_station_scan *, uint64_t now_ms,
    df_runtime_station_scan_emit_fn, void *context);
int df_runtime_sync_configure(struct df_gvs_runtime_sync *,
    const struct df_runtime_config *);
int df_runtime_media_build_module_config(const struct df_runtime_config *,
    const uint8_t local[6], struct df_media_station_config_v3 *, size_t,
    struct df_media_module_config_v3 *);
int df_runtime_receive_control_with_media(struct df_runtime_media_module *,
    struct df_runtime_ubus *, struct df_event_stream *,
    const struct df_station_registry *,
    struct df_gvs_call_control *,
    const uint8_t *data, size_t length,
    const uint8_t local[6], struct df_gvs_session *, struct df_gvs_deadline *,
    uint32_t source_ipv4, uint64_t now_ms,
    struct df_gvs_call_control_result *, int *media_result);
int df_runtime_media_push_video_with_event(struct df_runtime_media_module *,
    struct df_runtime_ubus *, const struct df_station_registry *,
    const struct df_gvs_video_packet *, uint32_t source_ipv4,
    uint64_t now_ms);
int df_runtime_media_tick_with_event(struct df_runtime_media_module *,
    struct df_runtime_ubus *, struct df_event_stream *, uint64_t now_ms);
int df_runtime_service_run(const struct df_runtime_config *runtime);
int df_runtime_resolve_station_credentials(const struct df_runtime_config *,
    struct df_station *, uint8_t material[8],
    enum df_station_unlock_mode *, enum df_access_credential_source *);

/* Control generations belong to the live runtime session; media generations
 * are allocated by the media module and must never be substituted here. */
uint64_t df_runtime_control_generation_next(uint64_t current);

#endif
