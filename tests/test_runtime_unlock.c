#include "runtime_ubus.h"
#include "runtime_config.h"
#include "media_session_manager.h"
#include "test.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

struct unlock_fixture {
    struct df_runtime_ubus u;
    struct df_runtime_config config;
    struct df_station stations[2];
    struct df_station_registry registry;
    struct df_gvs_access_control access;
    struct df_gvs_session call, control;
    struct df_runtime_media_module module;
    struct df_media_session_status_v3 session, snapshot[1];
    struct df_media_unlock_prepare_status_v1 preparation;
    unsigned sends, starts, stops, full_starts, hangups;
    int start_result, status_result, stop_result, send_result, media_result;
    uint8_t sent_material[8];
};
static int runtime_status(struct df_gvs_runtime_sync_status *s, void *c) {
    (void)c; memset(s, 0, sizeof(*s)); return DF_OK;
}
static int send_unlock(const struct df_gvs_access_request *r, void *c) {
    struct unlock_fixture *f = c;
    f->sends++;
    memcpy(f->sent_material, r->material, 8);
    return f->send_result;
}
static int hangup(const struct df_runtime_call_request *r, uint64_t now, void *c) {
    struct unlock_fixture *f = c;
    (void)now;
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_COMMAND_HANGUP, r->type);
    TEST_ASSERT_INT_EQ(3, r->session_generation);
    f->hangups++;
    return DF_OK;
}
static int media_status(const void *c, struct df_media_module_status_v3 *s) {
    const struct unlock_fixture *f = c;
    if (f->media_result != DF_OK) return f->media_result;
    s->required_session_count = f->session.active ? 1 : 0;
    if (s->sessions != NULL && s->session_count && f->session.active) {
        s->sessions[0] = f->session; s->session_count = 1;
    } else s->session_count = 0;
    s->configured_capacity = 1;
    return DF_OK;
}
static int full_start(void *c, const char *id, enum df_media_session_purpose p,
    uint64_t g, uint64_t now) {
    (void)id; (void)p; (void)g; (void)now;
    ((struct unlock_fixture *)c)->full_starts++; return DF_OK;
}
static int prepare_start(void *c, const char *id, uint64_t now, uint64_t *g) {
    struct unlock_fixture *f = c;
    (void)now;
    f->starts++;
    if (f->start_result) return f->start_result;
    f->session.active = true;
    snprintf(f->session.station_id, sizeof(f->session.station_id), "%s", id);
    f->session.generation = 7;
    f->session.state = DF_MEDIA_SESSION_REQUESTING;
    f->preparation.active = true;
    f->preparation.generation = 7;
    f->preparation.attempt_generation = 7;
    *g = 7;
    return DF_OK;
}
static int prepare_status(const void *c, const char *id, uint64_t g,
    struct df_media_unlock_prepare_status_v1 *s) {
    const struct unlock_fixture *f = c;
    (void)id; (void)g; *s = f->preparation; return f->status_result;
}
static int prepare_stop(void *c, const struct df_media_session_key *k, uint64_t now) {
    struct unlock_fixture *f = c;
    (void)now;
    TEST_ASSERT_INT_EQ(7, k->generation);
    TEST_ASSERT_INT_EQ(0, strcmp(k->station_id, "gate_main"));
    f->stops++;
    return f->stop_result;
}
static const struct df_media_module_api_v3 media_api = {
    .abi_version = DF_MEDIA_MODULE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_api_v3),
    .start = full_start, .status = media_status
};
static const struct df_media_module_unlock_prepare_api_v1 prepare_api = {
    .abi_version = DF_MEDIA_MODULE_UNLOCK_PREPARE_ABI_VERSION,
    .struct_size = sizeof(struct df_media_module_unlock_prepare_api_v1),
    .start = prepare_start, .status = prepare_status, .stop = prepare_stop
};
static void init(struct unlock_fixture *f) {
    static const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    memset(f, 0, sizeof(*f));
    f->stations[0] = (struct df_station){.id = "gate_main", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 1, 0}};
    f->stations[1] = (struct df_station){.id = "gate_side", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 2, 0}};
    f->registry.items = f->stations; f->registry.count = 2;
    f->module = (struct df_runtime_media_module){.api = &media_api,
        .instance = f, .available = true, .session_snapshot = f->snapshot,
        .session_snapshot_capacity = 1, .unlock_prepare_api = &prepare_api};
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(&f->access,
        "0011223344556677", 0, send_unlock, f));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_start(&f->u, runtime_status, f, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_access(&f->u, &f->access, &f->call, local));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_station_access(&f->u, &f->control, &f->registry));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_media(&f->u, &f->module,
        "/tmp/doorfast-unlock-unused-media-credentials"));
    df_runtime_ubus_set_active_host(&f->u, true);
}
static void ready(struct unlock_fixture *f) {
    f->preparation.ready = f->preparation.source_live = f->preparation.current_attempt = true;
    f->preparation.first_frame_ms = 120;
    f->preparation.attempts = 2;
    f->preparation.busy_replies = 1;
    f->preparation.has_first_request = true;
    f->preparation.first_request_ms = 105;
}
static void reply(struct unlock_fixture *f, uint64_t now, uint8_t raw) {
    struct df_gvs_frame frame = {.family = 4, .opcode = 0x89,
        .payload = &raw, .payload_length = 1};
    memcpy(frame.source, f->control.peer, 6);
    memcpy(frame.destination, f->u.access_identity, 6);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_observe(&f->access.result,
        &f->control, f->u.access_identity, &frame, now));
}

/* The same operation sink used by ubus, driven by actual coordinator states.
 * The fixed envelope is the additive public status contract, not a target
 * libubus build. No credential or address is copied into the output. */
static void json_string(void *c, const char *key, const char *value) {
    bool *comma = c;
    printf("%s\"%s\":\"%s\"", *comma ? "," : "", key, value);
    *comma = true;
}
static void json_number(void *c, const char *key, uint64_t value) {
    bool *comma = c;
    printf("%s\"%s\":%llu", *comma ? "," : "", key, (unsigned long long)value);
    *comma = true;
}
static void json_boolean(void *c, const char *key, bool value) {
    bool *comma = c;
    printf("%s\"%s\":%s", *comma ? "," : "", key, value ? "true" : "false");
    *comma = true;
}
static void json_status(struct unlock_fixture *f, uint64_t now) {
    bool comma = false;
    struct df_runtime_unlock_sink sink = {&comma, json_string, json_number, json_boolean};
    printf("{\"runtime_id\":\"%s\",\"access\":{\"version\":1,\"stations\":[", f->u.runtime_id);
    for (size_t i = 0; i < f->registry.count; ++i) {
        struct df_runtime_unlock_station metadata;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_unlock_station_metadata(&f->u, &f->stations[i], &metadata));
        printf("%s{\"station_id\":\"%s\",\"mode\":\"%s\",\"configured\":%s,"
            "\"credential_source\":\"%s\",\"reason\":\"%s\"}", i ? "," : "", f->stations[i].id,
            df_runtime_unlock_mode_name(metadata.mode), metadata.configured ? "true" : "false",
            df_runtime_unlock_source_name(metadata.credential_source), metadata.reason);
    }
    printf("],\"requests\":[");
    bool emitted = false;
    for (size_t i = 0; i < f->u.unlock_record_count; ++i) {
        if (!df_runtime_unlock_operation_valid(&f->u.unlock_records[i])) continue;
        printf("%s{", emitted ? "," : ""); comma = false;
        df_runtime_unlock_emit_operation(&f->u.unlock_records[i], now, &sink);
        putchar('}'); emitted = true;
    }
    putchar(']');
    if (df_runtime_unlock_operation_valid(&f->u.unlock)) {
        printf(",\"request\":{");
        comma = false;
        df_runtime_unlock_emit_operation(&f->u.unlock, now, &sink);
        putchar('}');
    }
    comma = true;
    json_boolean(&comma, "configured", f->access.configured);
    json_string(&comma, "state", df_gvs_access_state_name(f->access.result.state));
    json_number(&comma, "generation", f->access.result.session_generation);
    const char *station = df_runtime_ubus_access_station_id(&f->u);
    if (station != NULL) json_string(&comma, "station_id", station);
    if (f->access.result.state == DF_GVS_ACCESS_PROTOCOL_COMPLETED ||
        f->access.result.state == DF_GVS_ACCESS_PROTOCOL_REJECTED)
        json_number(&comma, "raw_status", f->access.result.raw_status);
    json_boolean(&comma, "physical_result_confirmed", false);
    json_string(&comma, "manual_state", f->u.manual_unlock_pending ? "waiting_media" :
        f->u.manual_unlock_inflight ? "waiting_reply" : "idle");
    if (f->u.manual_unlock_pending || f->u.manual_unlock_inflight)
        json_string(&comma, "manual_station_id", f->u.manual_unlock_station_id);
    printf("}}");
}
void test_runtime_unlock_json_fixtures(void) {
    const char *names[] = {"accepted_preparing", "sent_waiting_reply", "protocol_completed",
        "protocol_rejected", "preparation_timeout", "reply_timeout", "cancelled", "runtime_reset"};
    putchar('[');
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        struct unlock_fixture f;
        uint64_t now = 100;
        init(&f);
        strcpy(f.u.runtime_id, "0123456789abcdef");
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", now));
        printf("%s{\"case\":\"%s\",\"accepted\":{\"submitted\":true,"
            "\"station_id\":\"gate_main\",\"runtime_id\":\"%s\",\"request_id\":\"%s\","
            "\"phase\":\"%s\"},\"status\":", i ? "," : "", names[i], f.u.runtime_id,
            f.u.unlock.request_id, df_runtime_unlock_phase(f.u.unlock.stage));
        if ((i >= 1 && i <= 3) || i == 5) {
            ready(&f); now = 120;
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now));
        }
        if (i == 2 || i == 3) {
            now = 150; reply(&f, now, i == 2 ? 1 : 0);
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now));
        } else if (i == 4 || i == 5) {
            now = i == 4 ? 30100 : 1120;
            if (i == 5) TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(
                &f.access.result, &f.control, f.u.access_identity, now));
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now));
        } else if (i == 6) {
            now = 110; df_runtime_unlock_cancel(&f.u, now);
        } else if (i == 7) {
            df_runtime_ubus_stop(&f.u); init(&f);
            strcpy(f.u.runtime_id, "fedcba9876543210");
        }
        json_status(&f, now); putchar('}');
        df_runtime_ubus_stop(&f.u);
    }
    puts("]");
}

void test_runtime_unlock_credential_mutation_isolation(void) {
    struct unlock_fixture f;
    char path[] = "/tmp/doorfast-unlock-mutation-XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    close(fd); unlink(path);
    init(&f);
    strcpy(f.config.access_credentials_path, path);
    f.stations[0].unlock_mode = DF_STATION_UNLOCK_DIRECT;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_unlock_config(&f.u, &f.config));
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_set(path, "gate_main",
        DF_STATION_UNLOCK_DIRECT, "0011223344556677"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    struct df_runtime_access_status status;
    struct df_runtime_station_field fields[] = {
        {"station_id", DF_STATION_FIELD_STRING, "gate_side", 0},
        {"material", DF_STATION_FIELD_STRING, "8899aabbccddeeff", 0},
        {"mode", DF_STATION_FIELD_STRING, "direct", 0}};
    struct df_runtime_unlock_operation before = f.u.unlock;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_access_credentials(&f.u,
        DF_RUNTIME_ACCESS_SET, fields, 3, &status));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &f.u.unlock, sizeof(before)));
    TEST_ASSERT_INT_EQ(0, f.stops);
    TEST_ASSERT_INT_EQ(1, f.u.manual_unlock_owns_preview);
    fields[0].string = "gate_main";
    uint8_t bytes[4096], after[4096];
    fd = open(path, O_RDONLY); ssize_t size = read(fd, bytes, sizeof(bytes)); close(fd);
    TEST_ASSERT_INT_EQ(1, size > 0);
    f.stop_result = DF_ERR_IO;
    for (unsigned clear = 0; clear < 2; ++clear) {
        f.u.unlock.stage = DF_RUNTIME_UNLOCK_PREPARING;
        TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_ubus_access_credentials(&f.u,
            clear ? DF_RUNTIME_ACCESS_CLEAR : DF_RUNTIME_ACCESS_SET,
            fields, clear ? 1 : 3, &status));
        TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
        TEST_ASSERT_INT_EQ(1, f.u.unlock.cleanup_pending);
        fd = open(path, O_RDONLY); ssize_t count = read(fd, after, sizeof(after)); close(fd);
        TEST_ASSERT_INT_EQ(size, count);
        TEST_ASSERT_INT_EQ(0, memcmp(bytes, after, (size_t)size));
    }
    f.stop_result = DF_OK;
    df_runtime_ubus_stop(&f.u); unlink(path);
}
void test_runtime_unlock_lifecycle(void) {
    /* Production order: receive (manual auto-handler is a no-op), protocol
     * clock tick, then manual projection. The receive time must survive. */
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        struct unlock_fixture ordered;
        uint64_t received = scenario == 1 ? 1119 : 150;
        uint64_t tick = scenario == 1 ? 1121 : 175;
        init(&ordered);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&ordered.u, "gate_main", 100));
        ready(&ordered);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&ordered.u, 120));
        reply(&ordered, received, scenario == 2 ? 0 : 1);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_handle_auto_unlock_result(&ordered.u, received));
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(&ordered.access.result,
            &ordered.control, ordered.u.access_identity, tick));
        TEST_ASSERT_INT_EQ(tick, ordered.access.result.last_now_ms);
        if (scenario == 2) ordered.media_result = DF_ERR_IO;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&ordered.u, tick));
        if (scenario == 2) {
            TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_WAITING_REPLY, ordered.u.unlock.stage);
            ordered.media_result = DF_OK;
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(&ordered.access.result,
                &ordered.control, ordered.u.access_identity, 1121));
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&ordered.u, 1121));
        }
        TEST_ASSERT_INT_EQ(scenario == 2 ? DF_RUNTIME_UNLOCK_REJECTED :
            DF_RUNTIME_UNLOCK_COMPLETED, ordered.u.unlock.stage);
        TEST_ASSERT_INT_EQ(received, ordered.u.unlock.reply_ms);
        TEST_ASSERT_INT_EQ(received, ordered.u.unlock.ended_ms);
        TEST_ASSERT_INT_EQ(received - 120, ordered.u.unlock.reply_ms - ordered.u.unlock.sent_ms);
        TEST_ASSERT_INT_EQ(1, ordered.sends);
        df_runtime_ubus_stop(&ordered.u);
    }
    struct unlock_fixture f;
    init(&f);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_PREPARING, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(0, f.sends);
    TEST_ASSERT_INT_EQ(1, f.starts);
    TEST_ASSERT_INT_EQ(0, f.full_starts);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 110));
    TEST_ASSERT_INT_EQ(1, f.u.next_unlock_request);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock_station(&f.u, "gate_side", 110));
    f.preparation.busy = true; f.preparation.busy_replies = 1;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 115));
    TEST_ASSERT_INT_EQ(0, strcmp("busy", f.u.unlock.wait_reason));
    ready(&f);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 120));
    TEST_ASSERT_INT_EQ(1, f.sends);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_WAITING_REPLY, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(1120, f.access.result.deadline_ms);
    TEST_ASSERT_INT_EQ(2, f.u.unlock.attempts);
    TEST_ASSERT_INT_EQ(1, f.u.unlock.busy_replies);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 121));
    TEST_ASSERT_INT_EQ(1, f.sends);
    f.preparation.has_confirmed = true;
    f.preparation.confirmed_ms = 125;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 126));
    TEST_ASSERT_INT_EQ(1, f.u.unlock.has_confirmed);
    TEST_ASSERT_INT_EQ(125, f.u.unlock.confirmed_ms);
    reply(&f, 150, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 151));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_COMPLETED, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(150, f.u.unlock.ended_ms);
    TEST_ASSERT_INT_EQ(1, f.stops);
    TEST_ASSERT_INT_EQ(1, f.u.unlock_records[0].raw_status);
    f.session.active = false;
    f.preparation = (struct df_media_unlock_prepare_status_v1){0};
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_side", 160));
    TEST_ASSERT_INT_EQ(0, strcmp("0000000000000002", f.u.unlock.request_id));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_COMPLETED, f.u.unlock_records[0].stage);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_PREPARING, f.u.unlock_records[1].stage);
    f.session.viewer_active = true;
    df_runtime_ubus_stop(&f.u);
}
void test_runtime_unlock_failures(void) {
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        struct unlock_fixture f;
        init(&f);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
        if (scenario == 0) {
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 30100));
            TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_PREPARATION_TIMEOUT, f.u.unlock.stage);
        } else if (scenario == 1) {
            f.status_result = DF_ERR_IO;
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 120));
            TEST_ASSERT_INT_EQ(0, f.full_starts);
            TEST_ASSERT_INT_EQ(0, f.sends);
        } else if (scenario == 2) {
            f.session.generation = 8;
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 120));
            TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
            TEST_ASSERT_INT_EQ(0, f.stops);
        } else {
            ready(&f);
            if (scenario == 3) f.send_result = DF_ERR_IO;
            int result = df_runtime_ubus_tick_manual_unlock(&f.u, 120);
            TEST_ASSERT_INT_EQ(scenario == 3 ? DF_ERR_IO : DF_OK, result);
            if (scenario == 3) {
                TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_SEND_FAILED, f.u.unlock.stage);
                TEST_ASSERT_INT_EQ(0, f.u.unlock.has_sent);
            } else if (scenario == 4) {
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 1120));
                TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_REPLY_TIMEOUT, f.u.unlock.stage);
            } else if (scenario == 5) {
                f.session.viewer_active = true;
                reply(&f, 150, 0);
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 151));
                TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_REJECTED, f.u.unlock.stage);
                TEST_ASSERT_INT_EQ(0, f.stops);
            } else if (scenario == 6) {
                f.stop_result = DF_ERR_IO;
                reply(&f, 150, 1);
                TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_ubus_tick_manual_unlock(&f.u, 151));
                TEST_ASSERT_INT_EQ(1, f.u.unlock.cleanup_pending);
                TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock_station(&f.u, "gate_side", 152));
                f.stop_result = DF_OK;
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 153));
                TEST_ASSERT_INT_EQ(0, f.u.unlock.cleanup_pending);
                TEST_ASSERT_INT_EQ(2, f.stops);
            } else if (scenario == 7) {
                f.access.result.session_generation++;
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 150));
                TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
            } else {
                f.session.generation = 8;
                reply(&f, 150, 1);
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 151));
                TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
                TEST_ASSERT_INT_EQ(0, f.stops);
            }
        }
        df_runtime_ubus_stop(&f.u);
    }
    struct unlock_fixture f;
    init(&f); f.start_result = DF_ERR_IO;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(0, f.full_starts);
    df_runtime_ubus_stop(&f.u);
}

/* Catches waiting for JPEG after a current confirmation, sending a second
 * command after an uncertain result, and admitting stale module metadata. */
void test_runtime_unlock_confirmation_fast_path(void) {
    for (unsigned outcome = 0; outcome < 4; ++outcome) {
        struct unlock_fixture f;
        init(&f);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
        f.preparation.has_first_request = true;
        f.preparation.first_request_ms = 101;
        f.preparation.has_confirmed = true;
        f.preparation.confirmed_ms = 102;
        f.preparation.attempts = 1;
        f.session.state = DF_MEDIA_SESSION_AWAITING_VIDEO;
        if (outcome == 3) f.send_result = DF_ERR_IO;
        TEST_ASSERT_INT_EQ(outcome == 3 ? DF_ERR_IO : DF_OK,
            df_runtime_ubus_tick_manual_unlock(&f.u, 102));
        TEST_ASSERT_INT_EQ(1, f.sends);
        TEST_ASSERT_INT_EQ(0, f.u.unlock.has_first_media);
        TEST_ASSERT_INT_EQ(0, f.preparation.ready);
        TEST_ASSERT_INT_EQ(0, f.preparation.current_attempt);
        TEST_ASSERT_INT_EQ(0, f.full_starts);
        if (outcome < 2) reply(&f, 132, outcome == 0 ? 1 : 0);
        uint64_t now = outcome < 2 ? 133 : 1102;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now));
        TEST_ASSERT_INT_EQ(outcome == 0 ? DF_RUNTIME_UNLOCK_COMPLETED :
            outcome == 1 ? DF_RUNTIME_UNLOCK_REJECTED :
            outcome == 2 ? DF_RUNTIME_UNLOCK_REPLY_TIMEOUT : DF_RUNTIME_UNLOCK_SEND_FAILED,
            f.u.unlock.stage);
        ready(&f);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now + 1));
        TEST_ASSERT_INT_EQ(1, f.sends); /* No JPEG-triggered fallback after send. */
        df_runtime_ubus_stop(&f.u);
    }
    for (unsigned invalid = 0; invalid < 16; ++invalid) {
        struct unlock_fixture f;
        init(&f);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
        f.preparation.has_first_request = f.preparation.has_confirmed = true;
        f.preparation.first_request_ms = 101;
        f.preparation.confirmed_ms = 102;
        f.session.state = DF_MEDIA_SESSION_AWAITING_VIDEO;
        switch (invalid) {
        case 0: f.preparation.has_confirmed = false; break;
        case 1: f.preparation.confirmed_ms = 99; break;
        case 2: f.preparation.confirmed_ms = 104; break;
        case 3: f.preparation.has_first_request = false; break;
        case 4: f.preparation.first_request_ms = 99; break;
        case 5: f.preparation.first_request_ms = 103; break;
        case 6: f.preparation.attempt_generation = 0; break;
        case 7: f.preparation.busy = true; break;
        case 8: f.preparation.cooldown = true; break;
        case 9: f.preparation.active = false; break;
        case 10: f.session.state = DF_MEDIA_SESSION_STOPPING; break;
        case 11: f.session.generation++; break;
        case 12: f.u.auto_unlock_pending = true; break;
        case 13: strcpy(f.u.runtime_id, "0123456789abcdef"); break;
        case 14: f.session.viewer_active = true; break;
        case 15: f.session.purpose = DF_MEDIA_SESSION_CALL; break;
        }
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 103));
        TEST_ASSERT_INT_EQ(0, f.sends);
        df_runtime_ubus_stop(&f.u);
    }
}

static int monitor_emit(const uint8_t destination[6], uint32_t ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t length, void *context) {
    (void)destination; (void)ipv4; (void)source; (void)family; (void)opcode;
    (void)payload; (void)length; (void)context;
    return DF_OK; /* Only the external network transport is replaced. */
}
static int monitor_route(const uint8_t peer[6], uint64_t now, uint32_t *ipv4, void *c) {
    (void)peer; (void)now; (void)c; *ipv4 = 0x01020304U; return DF_OK;
}
static int monitor_memory(uint64_t *kib, void *c) {
    (void)c; *kib = 4096; return DF_OK;
}

/* Exercises the actual monitor -> optional capability -> runtime send chain.
 * Removing source/request/expiry/generation/release checks must admit one of
 * these invalid confirmations, not merely change an internal flag. */
void test_runtime_unlock_confirmation_protocol_guards(void) {
    for (unsigned scenario = 0; scenario < 13; ++scenario) {
        struct unlock_fixture f;
        struct df_media_session_manager manager = {0};
        struct df_media_station_config_v3 station = {.id = "gate_main",
            .stream_name = "doorfast_gate_main", .enabled = true, .ipv4 = 0x01020304U,
            .logical_address = {0x32, 2, 1, 0, 1, 0}};
        struct df_media_module_config_v3 config = {.enabled = true,
            .stations = &station, .station_count = 1, .max_encoders = 1,
            .first_frame_timeout_s = 8, .fps = 10,
            .incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW};
        struct df_media_module_callbacks_v3 callbacks = {.emit_control = monitor_emit,
            .resolve_route = monitor_route, .available_memory = monitor_memory};
        const uint8_t body[] = {0x1e, 0, 1};
        struct df_gvs_frame ack = {.family = 3, .opcode = 0x84,
            .payload = body, .payload_length = sizeof(body)};
        uint64_t now = 102;
        init(&f);
        memcpy(config.local, f.u.access_identity, 6);
        memcpy(ack.source, station.logical_address, 6);
        memcpy(ack.destination, config.local, 6);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_init(&manager, &config, &callbacks));
        f.module.api = &df_media_module_api_v3;
        f.module.instance = &manager;
        f.module.unlock_prepare_api = &df_media_module_unlock_prepare_api_v1;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
        if (scenario != 1)
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, 101));
        uint32_t ipv4 = station.ipv4;
        if (scenario == 2) ack.source[5]++;
        if (scenario == 3) ack.destination[5]++;
        if (scenario == 4) ipv4++;
        if (scenario == 5) ack.payload_length--;
        if (scenario == 6) now = 1101; /* Exact request-window expiry. */
        if (scenario == 7) {
            struct df_gvs_frame busy = ack;
            busy.opcode = 0x50; busy.payload = NULL; busy.payload_length = 0;
            TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
                &manager, &busy, station.ipv4, 102));
            now = 103;
        }
        int received = df_media_session_manager_receive_control(&manager, &ack, ipv4, now);
        TEST_ASSERT_INT_EQ(scenario >= 2 && scenario <= 5 ? DF_ERR_INVALID : DF_OK, received);
        if (scenario >= 8) {
            now = 103;
            if (scenario == 8) {
                struct df_media_session_key key = {"gate_main", f.u.unlock.media_generation};
                TEST_ASSERT_INT_EQ(DF_OK, df_media_module_unlock_prepare_api_v1.stop(
                    &manager, &key, now));
            } else if (scenario == 9) {
                now = 8102;
                TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager, now));
            } else if (scenario == 10) {
                const uint8_t stopped = 1;
                ack.opcode = 0x02; ack.payload = &stopped; ack.payload_length = 1;
                TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_receive_control(
                    &manager, &ack, station.ipv4, now));
            } else if (scenario == 11) {
                /* A retained confirmation from another native attempt. */
                manager.sessions[0].preview_confirmation_generation++;
            } else {
                /* A monitor replacement not yet bound to the media attempt. */
                manager.sessions[0].monitor.generation++;
                manager.sessions[0].preview_confirmation_generation++;
            }
        }
        struct df_media_unlock_prepare_status_v1 status;
        TEST_ASSERT_INT_EQ(DF_OK, df_media_module_unlock_prepare_api_v1.status(
            &manager, "gate_main", f.u.unlock.media_generation, &status));
        TEST_ASSERT_INT_EQ(scenario == 0, status.has_confirmed);
        TEST_ASSERT_INT_EQ(0, status.ready);
        TEST_ASSERT_INT_EQ(0, status.current_attempt);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, now));
        TEST_ASSERT_INT_EQ(scenario == 0, f.sends);
        TEST_ASSERT_INT_EQ(0, manager.sessions[0].encoder.running);
        df_runtime_ubus_stop(&f.u);
        df_media_session_manager_destroy(&manager);
    }
}

struct emitted { char text[4096]; size_t n; };
static void emit_string(void *c, const char *key, const char *value) {
    struct emitted *e = c;
    int n = snprintf(e->text + e->n, sizeof(e->text) - e->n, "%s=%s;", key, value);
    TEST_ASSERT_INT_EQ(1, n > 0 && (size_t)n < sizeof(e->text) - e->n);
    if (n > 0) e->n += (size_t)n;
}
static void emit_number(void *c, const char *key, uint64_t value) {
    char number[32]; snprintf(number, sizeof(number), "%llu", (unsigned long long)value);
    emit_string(c, key, number);
}
static void emit_boolean(void *c, const char *key, bool value) {
    emit_string(c, key, value ? "true" : "false");
}
void test_runtime_unlock_projection_and_credentials(void) {
    struct unlock_fixture f;
    struct emitted e = {0};
    struct df_runtime_unlock_sink sink = {&e, emit_string, emit_number, emit_boolean};
    init(&f);
    df_runtime_unlock_emit_operation(&f.u.unlock, 100, &sink);
    TEST_ASSERT_INT_EQ(0, e.n);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    df_runtime_unlock_emit_operation(&f.u.unlock, 110, &sink);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "request_id=0000000000000001;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "remaining_ms=29990;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "physical_result_confirmed=false;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "sent_ms=") == NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "first_media_ms=") == NULL);
    ready(&f);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 120));
    reply(&f, 150, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 151));
    e = (struct emitted){0};
    df_runtime_unlock_emit_operation(&f.u.unlock, 10000, &sink);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "phase=protocol_completed;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "elapsed_ms=50;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "remaining_ms=0;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "first_request_ms=5;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "first_media_ms=20;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "sent_ms=20;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "reply_latency_ms=30;") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(e.text, "material") == NULL);
    struct df_runtime_unlock_operation invalid = f.u.unlock;
    invalid.stage = (enum df_runtime_unlock_stage)999;
    e = (struct emitted){0};
    df_runtime_unlock_emit_operation(&invalid, 10000, &sink);
    TEST_ASSERT_INT_EQ(0, e.n);
    invalid = f.u.unlock; invalid.request_id[16] = 'a';
    df_runtime_unlock_emit_operation(&invalid, 10000, &sink);
    TEST_ASSERT_INT_EQ(0, e.n);
    f.u.next_unlock_request = UINT64_MAX;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_unlock_station(&f.u, "gate_side", 10001));
    df_runtime_ubus_stop(&f.u);

    init(&f);
    char path[] = "/tmp/doorfast-unlock-store-XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) { df_runtime_ubus_stop(&f.u); return; }
    TEST_ASSERT_INT_EQ(0, fchmod(fd, 0600)); close(fd);
    snprintf(f.config.access_credentials_path, sizeof(f.config.access_credentials_path), "%s", path);
    f.stations[0].unlock_mode = DF_STATION_UNLOCK_DIRECT;
    f.access.configured = false;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_unlock_config(&f.u, &f.config));
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_set(path, "gate_main",
        DF_STATION_UNLOCK_DIRECT, "0011223344556677"));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_access_credentials_set(path, "gate_main",
        DF_STATION_UNLOCK_DIRECT, "aabbccddeeff0011"));
    ready(&f);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 120));
    TEST_ASSERT_INT_EQ(1, f.sends);
    TEST_ASSERT_INT_EQ(0xaa, f.sent_material[0]);
    TEST_ASSERT_INT_EQ(DF_ACCESS_CREDENTIAL_SOURCE_STATION, f.u.unlock.credential_source);
    f.stations[0].unlock_mode = DF_STATION_UNLOCK_DISABLED;
    struct df_runtime_unlock_station metadata;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_unlock_station_metadata(&f.u, &f.stations[0], &metadata));
    TEST_ASSERT_INT_EQ(0, strcmp("disabled", metadata.reason));
    f.stations[0].unlock_mode = DF_STATION_UNLOCK_INHERIT;
    fd = open(path, O_WRONLY | O_TRUNC);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd >= 0) { TEST_ASSERT_INT_EQ(8, write(fd, "invalid\n", 8)); close(fd); }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_unlock_station_metadata(&f.u, &f.stations[0], &metadata));
    TEST_ASSERT_INT_EQ(0, strcmp("store_unavailable", metadata.reason));
    unlink(path);
    df_runtime_ubus_stop(&f.u);
}

void test_runtime_unlock_auto_and_reset(void) {
    /* A terminal local cancellation must release legacy auto ownership even
     * when the independently accepted wire result is completed/rejected. */
    for (unsigned removed = 0; removed < 2; ++removed) {
        struct unlock_fixture cancelled;
        init(&cancelled);
        cancelled.call.state = DF_GVS_RINGING; cancelled.call.generation = 3;
        memcpy(cancelled.call.peer, cancelled.stations[0].logical_address, 6);
        cancelled.session = (struct df_media_session_status_v3){.station_id = "gate_main",
            .generation = 7, .purpose = DF_MEDIA_SESSION_CALL, .active = true, .ready = true};
        cancelled.u.submit_call = hangup; cancelled.u.call_context = &cancelled;
        df_runtime_ubus_set_auto_unlock(&cancelled.u, true);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_schedule_auto_unlock(&cancelled.u,
            "gate_main", &cancelled.call, 100));
        struct df_runtime_media_status status = {.sessions = &cancelled.session, .session_count = 1};
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_auto_unlock(&cancelled.u, &status, 100));
        TEST_ASSERT_INT_EQ(1, cancelled.u.auto_unlock_inflight);
        cancelled.session.generation = 8;
        if (removed) cancelled.session.active = false;
        reply(&cancelled, 150, removed ? 0 : 1);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_handle_auto_unlock_result(&cancelled.u, 150));
        TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, cancelled.u.unlock.stage);
        TEST_ASSERT_INT_EQ(0, cancelled.u.auto_unlock_inflight);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_auto_unlock(&cancelled.u, &status, 151));
        TEST_ASSERT_INT_EQ(0, cancelled.u.auto_unlock_inflight);
        TEST_ASSERT_INT_EQ(0, cancelled.hangups);
        TEST_ASSERT_INT_EQ(1, cancelled.sends);
        /* Admit a new explicit manual request while the same call remains
         * active, proving the stale auto flag no longer causes contention. */
        cancelled.session.active = true; cancelled.session.ready = false;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&cancelled.u, "gate_main", 152));
        TEST_ASSERT_INT_EQ(1, cancelled.u.manual_unlock_pending);
        TEST_ASSERT_INT_EQ(1, cancelled.sends);
        df_runtime_ubus_stop(&cancelled.u);
    }
    struct unlock_fixture f;
    init(&f);
    snprintf(f.config.access_credentials_path, sizeof(f.config.access_credentials_path),
        "%s", "/tmp/doorfast-unlock-absent-auto-store");
    snprintf(f.config.access_material, sizeof(f.config.access_material), "%s", "8899aabbccddeeff");
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_bind_unlock_config(&f.u, &f.config));
    f.call.state = DF_GVS_RINGING; f.call.generation = 3;
    memcpy(f.call.peer, f.stations[0].logical_address, 6);
    f.session = (struct df_media_session_status_v3){.station_id = "gate_main",
        .generation = 7, .purpose = DF_MEDIA_SESSION_CALL, .active = true};
    f.u.submit_call = hangup; f.u.call_context = &f;
    df_runtime_ubus_set_auto_unlock(&f.u, true);
    df_runtime_ubus_set_auto_unlock_delay(&f.u, 2);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_schedule_auto_unlock(&f.u, "gate_main", &f.call, 100));
    struct df_runtime_media_status media = {.sessions = &f.session, .session_count = 1};
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_auto_unlock(&f.u, &media, 2000));
    TEST_ASSERT_INT_EQ(0, f.sends);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_auto_unlock(&f.u, &media, 2100));
    TEST_ASSERT_INT_EQ(0, f.sends);
    f.session.ready = true;
    /* Auto retains its original CALL+ready gate, including source_live=false. */
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_auto_unlock(&f.u, &media, 2101));
    TEST_ASSERT_INT_EQ(1, f.sends);
    TEST_ASSERT_INT_EQ(0x88, f.sent_material[0]);
    TEST_ASSERT_INT_EQ(1, f.u.unlock.automatic);
    reply(&f, 2200, 1);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_handle_auto_unlock_result(&f.u, 2200));
    TEST_ASSERT_INT_EQ(1, f.hangups);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_COMPLETED, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_COMPLETED, f.u.unlock_records[0].stage);
    df_runtime_ubus_stop(&f.u);
    init(&f);
    TEST_ASSERT_INT_EQ(0, f.u.unlock.request_id[0]);
    TEST_ASSERT_INT_EQ(0, f.u.next_unlock_request);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_unlock_station(&f.u, "gate_main", 100));
    f.u.runtime_id[0] = f.u.runtime_id[0] == 'a' ? 'b' : 'a';
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_tick_manual_unlock(&f.u, 110));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_UNLOCK_CANCELLED, f.u.unlock.stage);
    TEST_ASSERT_INT_EQ(0, f.sends);
    df_runtime_ubus_stop(&f.u);
}
