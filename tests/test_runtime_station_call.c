#include <stdio.h>
#include <string.h>
#include "runtime_station_call.h"
#include "media_session_manager.h"
#include "gvs_memory_sender.h"
#include "test.h"

struct call_fixture {
    struct df_runtime_ubus ubus;
    struct df_runtime_call_binding binding;
    struct df_gvs_call_control control;
    struct df_gvs_session session;
    struct df_gvs_deadline deadline;
    struct df_runtime_media_module media;
    struct df_station stations[2];
    struct df_station_registry registry;
    struct df_media_module_callbacks_v3 callbacks;
    unsigned previews, calls, hangups;
    bool stop_fails;
    enum df_gvs_send_attempt_result transport;
};
static const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
static int emit(const uint8_t to[6], uint32_t ip, const uint8_t from[6],
    uint8_t family, uint8_t opcode, const uint8_t *payload, size_t size, void *ctx) {
    struct call_fixture *f = ctx;
    (void)to; (void)ip; (void)from; (void)family; (void)payload; (void)size;
    if (opcode == 4) f->previews++;
    if (opcode == 2) f->hangups++;
    if (opcode == 2 && f->stop_fails) return DF_ERR_IO;
    return DF_OK;
}
static int route(const uint8_t peer[6], uint64_t now, uint32_t *ip, void *ctx) {
    (void)peer; (void)now; (void)ctx; *ip = 0x01020304; return DF_OK;
}
static int memory(uint64_t *size, void *ctx) {
    (void)ctx; *size = 4096; return DF_OK;
}
static int status(struct df_gvs_call_control_status *out, void *ctx) {
    struct df_runtime_call_binding *b = ctx;
    return df_gvs_call_control_status_with_deadline(b->control, b->session, b->deadline, out);
}
static enum df_gvs_send_attempt_result transport(const struct df_gvs_call_command *c,
    unsigned attempt, uint64_t id, void *ctx) {
    struct call_fixture *f = ctx;
    (void)attempt; (void)id;
    if (c->type == DF_GVS_CALL_COMMAND_CALL) {
        const uint8_t expected[] = {2, 0x20, 0x6f, 0, 0x20, 0x6e, 120};
        TEST_ASSERT_INT_EQ(0, memcmp(expected, c->payload, sizeof(expected)));
        TEST_ASSERT_INT_EQ(0, memcmp(f->stations[0].logical_address, c->destination, 6));
        f->calls++;
    }
    if (c->type == DF_GVS_CALL_COMMAND_HANGUP) f->hangups++;
    return f->transport;
}
static void init(struct call_fixture *f) {
    memset(f, 0, sizeof(*f));
    f->stations[0] = (struct df_station){.id = "gate_main", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 2, 0}};
    f->stations[1] = (struct df_station){.id = "gate_side", .enabled = true,
        .logical_address = {0x32, 2, 1, 0, 3, 0}};
    f->registry = (struct df_station_registry){.items = f->stations, .count = 2};
    struct df_media_station_config_v3 stations[2] = {
        {.id = "gate_main", .stream_name = "doorfast_gate_main", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 2, 0}, .ipv4 = 0x01020304},
        {.id = "gate_side", .stream_name = "doorfast_gate_side", .enabled = true,
            .logical_address = {0x32, 2, 1, 0, 3, 0}, .ipv4 = 0x01020304}};
    struct df_media_module_config_v3 config = {.enabled = true,
        .local = {0x61, 2, 1, 1, 1, 1}, .stations = stations, .station_count = 2,
        .max_encoders = 2, .fps = 10};
    f->callbacks = (struct df_media_module_callbacks_v3){.emit_control = emit,
        .resolve_route = route, .available_memory = memory, .context = f};
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_start_with_api(&f->media,
        &df_media_module_api_v3, &config, &f->callbacks));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_set_outbound_api(&f->media,
        &df_media_module_outbound_upgrade_api_v1));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_set_lifecycle_api(&f->media,
        &df_media_module_lifecycle_api_v2));
    f->binding = (struct df_runtime_call_binding){.ubus = &f->ubus,
        .control = &f->control, .session = &f->session, .deadline = &f->deadline,
        .identity = local, .stations = &f->registry};
    f->ubus = (struct df_runtime_ubus){.started = true, .active_host = true,
        .runtime_id = "1111111111111111", .last_now_ms = 100,
        .provide_call_status = status, .submit_call = df_runtime_call_submit,
        .call_context = &f->binding, .media = &f->media};
    f->session.generation = 40;
    f->transport = DF_GVS_SEND_ATTEMPT_SUCCESS;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_init(&f->control, 0,
        df_gvs_placeholder_header_fields, NULL));
    df_gvs_call_control_set_sender(&f->control, transport, f);
}
static const struct df_media_session *media(struct call_fixture *f) {
    return df_media_session_manager_find(f->media.instance, "gate_main");
}
static void admit(struct call_fixture *f) {
    uint64_t generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_station(&f->ubus,
        f->ubus.runtime_id, "gate_main", &generation));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_PREPARING, f->ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f->media, f->ubus.last_now_ms));
}
static void receive(struct call_fixture *f, uint8_t opcode, const uint8_t *payload,
    size_t size, unsigned station, uint32_t ip, uint64_t now) {
    uint8_t bytes[64] = {0};
    struct df_gvs_call_control_result result;
    int media_result;
    size_t length;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_control_serialize(bytes, sizeof(bytes), &length,
        local, f->stations[station].logical_address, 3, opcode, payload, (uint16_t)size,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_receive_control_with_media(&f->media,
        &f->ubus, NULL, &f->registry, &f->control, bytes, length, local,
        &f->session, &f->deadline, ip, now, &result, &media_result));
    f->ubus.last_now_ms = now;
}
static void confirm(struct call_fixture *f) {
    const uint8_t ack[] = {0x1e, 0, 1};
    receive(f, 0x84, ack, sizeof(ack), 0, 0x01020304, 102);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_CALLING, f->ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(41, f->session.generation);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_QUEUED, f->control.dispatch.state);
    TEST_ASSERT_INT_EQ(0, f->ubus.station_call.command_sent);
    TEST_ASSERT_INT_EQ(0, media(f)->outbound_command_sent);
}
static void step(struct call_fixture *f, uint64_t now) {
    struct df_gvs_call_control_result result;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_call_step(&f->binding, now, &result));
    f->ubus.last_now_ms = now;
}
static void talking(struct call_fixture *f) {
    const uint8_t ack[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 120};
    admit(f); confirm(f); step(f, 103);
    receive(f, 0x83, ack, sizeof(ack), 0, 0x01020304, 104);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, f->session.state);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_TALKING, f->ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(1, f->calls);
    TEST_ASSERT_INT_EQ(1, media(f)->outbound_command_sent);
}

static int fail_encoder_stop(struct df_media_encoder_process *encoder, unsigned timeout) {
    (void)encoder; (void)timeout;
    return DF_ERR_IO;
}

/* A real manager behind the original v1 surface: newer opcodes unavailable. */
static int original_v1_operation(void *instance, enum df_media_outbound_operation op,
    const struct df_media_session_key *key, uint64_t control, uint64_t now) {
    if (op > DF_MEDIA_OUTBOUND_MARK_SENT) return DF_ERR_INVALID;
    return df_media_session_manager_outbound_operation(instance, op, key, control, now);
}

void test_runtime_station_call_legacy_capabilities(void) {
    static const struct df_media_module_outbound_upgrade_api_v1 original = {
        .abi_version = 1, .struct_size = sizeof(original), .operation = original_v1_operation};
    for (unsigned v1 = 0; v1 < 2; v1++) {
        struct call_fixture f;
        init(&f);
        f.media.outbound_upgrade_api = v1 ? &original : NULL;
        f.media.lifecycle_api = NULL;
        uint64_t generation = 0;
        TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station(&f.ubus,
            f.ubus.runtime_id, "gate_main", &generation) != DF_OK);
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        TEST_ASSERT_INT_EQ(0, f.previews);
        df_runtime_media_module_stop(&f.media);

        init(&f);
        f.media.outbound_upgrade_api = v1 ? &original : NULL;
        f.media.lifecycle_api = NULL;
        const uint8_t ring[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
        receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 101);
        TEST_ASSERT_INT_EQ(DF_GVS_RINGING, f.session.state);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
            f.ubus.runtime_id, "gate_main", media(&f)->generation));
        step(&f, 102);
        receive(&f, 0x82, NULL, 0, 0, 0x01020304, 103);
        TEST_ASSERT_INT_EQ(DF_GVS_ENDED, f.session.state);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 5103));
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        TEST_ASSERT_INT_EQ(1, f.hangups);
        df_runtime_media_module_stop(&f.media);

        init(&f);
        f.media.outbound_upgrade_api = v1 ? &original : NULL;
        f.media.lifecycle_api = NULL;
        receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 101);
        struct df_media_session_manager *manager = f.media.instance;
        manager->sessions[0].stop_encoder = fail_encoder_stop;
        uint64_t old_media = media(&f)->generation;
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED,
            df_runtime_media_module_release_incoming(&f.media, "gate_main", 41,
                DF_MEDIA_INCOMING_RELEASE_LOCAL, 102));
        struct df_media_session_status_v3 entries[2];
        struct df_media_module_status_v3 status = {.sessions = entries, .session_count = 2};
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_status(&f.media, &status));
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED, entries[0].last_error);
        TEST_ASSERT_INT_EQ(1, f.media.incoming_bindings[0].cleanup_pending);
        TEST_ASSERT_INT_EQ(0, f.hangups);
        manager->sessions[0].stop_encoder = NULL;
        char preempted[DF_MEDIA_MODULE_STATION_ID_MAX];
        uint64_t preempted_generation;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_incoming_call(&f.media,
            "gate_main", 42, 103, preempted, &preempted_generation));
        TEST_ASSERT_INT_EQ(1, media(&f)->generation != old_media);
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
            df_runtime_media_module_release_incoming(&f.media, "gate_main", 41,
                DF_MEDIA_INCOMING_RELEASE_LOCAL, 104));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 202));
        TEST_ASSERT_INT_EQ(1, manager->active_count);
        TEST_ASSERT_INT_EQ(42, media(&f)->call_generation);
        TEST_ASSERT_INT_EQ(0, f.hangups);
        df_runtime_media_module_stop(&f.media);
        TEST_ASSERT_INT_EQ(0, f.media.incoming_binding_count);

        init(&f);
        f.media.lifecycle_api = NULL;
        f.media.outbound_upgrade_api = v1 ? &original : NULL;
        receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 101);
        manager = f.media.instance;
        manager->sessions[0].stop_encoder = fail_encoder_stop;
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED,
            df_runtime_media_module_release_incoming(&f.media, "gate_main", 41,
                DF_MEDIA_INCOMING_RELEASE_LOCAL, 102));
        f.session.generation = 42;
        f.session.state = DF_GVS_RINGING;
        memcpy(f.session.peer, f.stations[1].logical_address, 6);
        manager->sessions[0].stop_encoder = NULL;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 202));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 302));
        TEST_ASSERT_INT_EQ(0, manager->active_count);
        TEST_ASSERT_INT_EQ(0, f.media.incoming_bindings[0].cleanup_pending);
        TEST_ASSERT_INT_EQ(42, f.session.generation);
        TEST_ASSERT_INT_EQ(DF_GVS_RINGING, f.session.state);
        TEST_ASSERT_INT_EQ(0, f.hangups);
        df_runtime_media_module_stop(&f.media);

        init(&f);
        f.media.lifecycle_api = NULL;
        f.media.outbound_upgrade_api = v1 ? &original : NULL;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_request_start(&f.media,
            "gate_main", 100, &generation));
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 100));
        const struct df_media_session_key preview = {"gate_main", generation};
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_command(&f.media,
            DF_MEDIA_MODULE_COMMAND_STOP, &preview, false, 101));
        TEST_ASSERT_INT_EQ(1, f.hangups);
        df_runtime_media_module_stop(&f.media);
    }
}

void test_runtime_station_call_release_failure_survives_main_replacement(void) {
    for (unsigned incoming = 0; incoming < 2; incoming++) {
        struct call_fixture f;
        init(&f);
        if (incoming) {
            const uint8_t ring[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
            receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 101);
        } else talking(&f);
        struct df_media_session_manager *manager = f.media.instance;
        struct df_media_session *old = &manager->sessions[0];
        const struct df_media_session_key key = {"gate_main", old->generation};
        old->stop_encoder = fail_encoder_stop;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
            f.ubus.runtime_id, "gate_main", key.generation));
        step(&f, 105);
        receive(&f, 0x82, NULL, 0, 0, 0x01020304, 106);
        TEST_ASSERT_INT_EQ(DF_GVS_ENDED, f.session.state);
        struct df_media_session_status_v3 entries[2];
        struct df_media_module_status_v3 status = {.sessions = entries, .session_count = 2};
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_status(&f.media, &status));
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_ENCODER_FAILED, entries[0].last_error);
        TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_STOPPING, entries[0].state);
        TEST_ASSERT_INT_EQ(1, manager->active_count);
        unsigned hangups = f.hangups;
        /* The runtime may move on; retained cleanup must not depend on ENDED. */
        f.session.generation = 42;
        f.session.state = DF_GVS_RINGING;
        memcpy(f.session.peer, f.stations[1].logical_address, 6);
        old->stop_encoder = NULL;
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 206));
        TEST_ASSERT_INT_EQ(0, manager->active_count);
        TEST_ASSERT_INT_EQ(hangups, f.hangups);
        TEST_ASSERT_INT_EQ(DF_GVS_RINGING, f.session.state);
        TEST_ASSERT_INT_EQ(42, f.session.generation);
        TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
            manager, "gate_main", 43, 207));
        uint64_t replacement = media(&f)->generation;
        TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_GENERATION_MISMATCH,
            df_runtime_media_module_outbound_operation(&f.media, incoming ?
                DF_MEDIA_INCOMING_RELEASE_LOCAL : DF_MEDIA_OUTBOUND_RELEASE,
                &key, 41, 208));
        TEST_ASSERT_INT_EQ(replacement, media(&f)->generation);
        TEST_ASSERT_INT_EQ(1, manager->active_count);
        df_runtime_media_module_stop(&f.media);
    }
}

void test_runtime_station_call_real_control_lifecycle(void) {
    struct call_fixture f;
    init(&f);
    f.session.pick_generation = 40;
    f.session.pick_started_ms = 10;
    f.session.pick_timeout_ms = 5000;
    memset(f.session.pick_local, 1, 6);
    f.control.fault = DF_GVS_CALL_FAULT_DISPATCH_FAILED;
    admit(&f);
    const uint8_t bad[] = {0x1e, 0, 2};
    receive(&f, 0x84, bad, sizeof(bad), 0, 0x01020304, 101);
    TEST_ASSERT_INT_EQ(DF_GVS_IDLE, f.session.state);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_EMPTY, f.control.dispatch.state);
    const uint8_t preview_ack[] = {0x1e, 0, 1};
    receive(&f, 0x84, preview_ack, sizeof(preview_ack), 0, 0x05060708, 101);
    receive(&f, 0x84, preview_ack, sizeof(preview_ack), 1, 0x01020304, 101);
    TEST_ASSERT_INT_EQ(DF_GVS_IDLE, f.session.state);
    confirm(&f);
    TEST_ASSERT_INT_EQ(0, f.session.pick_generation);
    TEST_ASSERT_INT_EQ(0, f.session.pick_started_ms);
    TEST_ASSERT_INT_EQ(0, f.session.pick_timeout_ms);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_NONE, f.control.fault);
    step(&f, 103);
    const uint8_t ack[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 120};
    receive(&f, 0x83, ack, sizeof(ack), 1, 0x01020304, 104);
    TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, f.session.state);
    receive(&f, 0x83, ack, sizeof(ack), 0, 0x01020304, 105);
    TEST_ASSERT_INT_EQ(DF_GVS_TALKING, f.session.state);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_TALKING, f.ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(1, f.calls);
    TEST_ASSERT_INT_EQ(1, f.previews);
    TEST_ASSERT_INT_EQ(1, f.session.generation != f.ubus.station_call.media_generation);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY, df_runtime_ubus_monitor_stop(&f.ubus,
        f.ubus.runtime_id, "gate_main", f.ubus.station_call.media_generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
        f.ubus.runtime_id, "gate_main", f.ubus.station_call.media_generation));
    step(&f, 106);
    receive(&f, 0x82, NULL, 0, 0, 0x01020304, 107);
    TEST_ASSERT_INT_EQ(DF_GVS_ENDED, f.session.state);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_ENDED, f.ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
    TEST_ASSERT_INT_EQ(1, f.hangups);
    TEST_ASSERT_INT_EQ(0, f.control.handshake.active);
    df_runtime_media_module_stop(&f.media);
}

void test_runtime_station_call_timeout_and_rollback(void) {
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        struct call_fixture f;
        init(&f);
        uint64_t generation;
        if (scenario == 1 || scenario == 3)
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_request_start(&f.media,
                "gate_main", 99, &generation));
        admit(&f);
        generation = f.ubus.station_call.media_generation;
        if (scenario == 2)
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_viewer(&f.ubus,
                f.ubus.runtime_id, "gate_main", generation, true));
        if (scenario >= 3) confirm(&f);
        if (scenario == 4) {
            step(&f, 103); step(&f, 1103);
            TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_ENDING, f.ubus.station_call.stage);
            step(&f, 1104); step(&f, 2104);
            TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_CONFIRMATION_TIMEOUT, f.ubus.station_call.end_reason);
        } else if (scenario == 5) {
            f.transport = DF_GVS_SEND_ATTEMPT_FAILURE;
            step(&f, 103); step(&f, 603); step(&f, 1103); step(&f, 1603);
            TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
        } else if (scenario == 6) {
            f.transport = DF_GVS_SEND_ATTEMPT_PENDING;
            step(&f, 103); step(&f, 353); step(&f, 453);
            step(&f, 703); step(&f, 803); step(&f, 1053);
            TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_FAULT_DISPATCH_TIMEOUT, f.ubus.station_call.fault);
        } else {
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
                f.ubus.runtime_id, "gate_main", generation));
            TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_ENDED, f.ubus.station_call.stage);
        }
        if (scenario == 1 || scenario == 2 || scenario == 3) {
            TEST_ASSERT_INT_EQ(1, media(&f)->active);
            TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, media(&f)->purpose);
            TEST_ASSERT_INT_EQ(generation, media(&f)->generation);
        } else {
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 10000));
            TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        }
        if (scenario < 4) TEST_ASSERT_INT_EQ(0, f.calls);
        df_runtime_media_module_stop(&f.media);
    }
}

void test_runtime_station_call_cleanup_error_and_shared_owner(void) {
    struct call_fixture f;
    init(&f); admit(&f);
    f.stop_fails = true;
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_ubus_hangup_station(&f.ubus,
        f.ubus.runtime_id, "gate_main", 0));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(DF_ERR_IO, f.ubus.station_call.cleanup_error);
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 10000));
    TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
    df_runtime_media_module_stop(&f.media);

    init(&f); admit(&f);
    uint64_t generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_monitor_start(&f.ubus,
        f.ubus.runtime_id, "gate_main", &generation));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
        f.ubus.runtime_id, "gate_main", generation));
    TEST_ASSERT_INT_EQ(1, media(&f)->active);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, media(&f)->purpose);
    TEST_ASSERT_INT_EQ(0, f.hangups);
    df_runtime_media_module_stop(&f.media);
}

void test_runtime_station_call_deadlines_and_identity(void) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        struct call_fixture f;
        init(&f);
        if (scenario == 0) {
            admit(&f); step(&f, 30100);
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_CONFIRMATION_TIMEOUT, f.ubus.station_call.end_reason);
            TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 40100));
        } else {
            talking(&f);
            if (scenario == 1) {
                TEST_ASSERT_INT_EQ(DF_OK, df_gvs_deadline_arm(&f.deadline, &f.session, 104, 5));
                step(&f, 109);
                TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_SESSION_TIMEOUT, f.ubus.station_call.end_reason);
            } else if (scenario == 2) {
                for (uint64_t now = 2103; now <= 10103; now += 2000) step(&f, now);
                TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_HEARTBEAT_TIMEOUT, f.ubus.station_call.end_reason);
            } else if (scenario == 3) {
                TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_tick_with_event(&f.media,
                    &f.ubus, NULL, 15102));
                step(&f, 15102); step(&f, 15103); step(&f, 16103);
                TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
                TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_VIDEO_STALLED, f.ubus.station_call.error);
                TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_UNKNOWN, f.ubus.station_call.end_reason);
            } else {
                df_runtime_station_call_shutdown(&f.ubus, 105);
                TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_RUNTIME_RESET, f.ubus.station_call.end_reason);
            }
        }
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        TEST_ASSERT_INT_EQ(0, f.control.handshake.active);
        if (scenario > 0) {
            struct df_media_session_diagnostics_v2 history[2];
            struct df_media_module_diagnostics_v2 diagnostics = {
                .last_results = history, .last_result_count = 2};
            TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(
                f.media.instance, 40100, &diagnostics));
            TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
            TEST_ASSERT_INT_EQ(scenario == 1 ? DF_MEDIA_END_SESSION_TIMEOUT :
                scenario == 4 ? DF_MEDIA_END_RUNTIME_RESET : DF_MEDIA_END_UNKNOWN,
                history[0].end_reason);
            if (scenario == 3)
                TEST_ASSERT_INT_EQ(DF_MEDIA_FAILURE_FIRST_FRAME_TIMEOUT, history[0].failure_reason);
        }
        df_runtime_media_module_stop(&f.media);
    }
    struct call_fixture f;
    init(&f); admit(&f);
    uint64_t generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_call_station(&f.ubus,
        f.ubus.runtime_id, "gate_main", &generation));
    TEST_ASSERT_INT_EQ(1, f.ubus.station_call.operation_id);
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_CAPACITY_BUSY, df_runtime_ubus_call_station(&f.ubus,
        f.ubus.runtime_id, "gate_side", &generation));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_MEDIA_ERROR_RUNTIME_MISMATCH, df_runtime_ubus_hangup_station(
        &f.ubus, "2222222222222222", "gate_main", 0));
    TEST_ASSERT_INT_EQ(DF_MEDIA_ERROR_STATION_NOT_FOUND, df_runtime_ubus_hangup_station(
        &f.ubus, f.ubus.runtime_id, "absent", 0));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_hangup_station(&f.ubus,
        f.ubus.runtime_id, "gate_main", 999));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
        f.ubus.runtime_id, "gate_side", 0));
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_PREPARING, f.ubus.station_call.stage);
    df_runtime_media_module_stop(&f.media);
}

void test_runtime_station_call_terminal_reasons(void) {
    struct call_fixture f;
    init(&f); talking(&f);
    const uint8_t reason[] = {1};
    receive(&f, 2, reason, sizeof(reason), 0, 0x01020304, 105);
    TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PEER_HANGUP, f.ubus.station_call.end_reason);
    struct df_media_session_diagnostics_v2 history[2];
    struct df_media_module_diagnostics_v2 diagnostics = {.last_results = history,
        .last_result_count = 2};
    TEST_ASSERT_INT_EQ(DF_OK, df_media_module_diagnostics_api_v2.status(f.media.instance,
        106, &diagnostics));
    TEST_ASSERT_INT_EQ(1, diagnostics.last_result_count);
    TEST_ASSERT_INT_EQ(DF_MEDIA_END_PEER_HANGUP, history[0].end_reason);
    TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
    df_runtime_media_module_stop(&f.media);
}

void test_runtime_station_call_incoming_and_limits(void) {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        struct call_fixture f;
        init(&f); admit(&f);
        if (scenario > 0) confirm(&f);
        if (scenario == 2) step(&f, 103);
        uint64_t old_media = f.ubus.station_call.media_generation;
        const uint8_t ring[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
        receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 104);
        TEST_ASSERT_INT_EQ(DF_GVS_RINGING, f.session.state);
        TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
        TEST_ASSERT_INT_EQ(DF_GVS_CALL_END_PREEMPTED, f.ubus.station_call.end_reason);
        TEST_ASSERT_INT_EQ(1, media(&f)->generation != old_media);
        TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL, media(&f)->purpose);
        TEST_ASSERT_INT_EQ(0, media(&f)->outbound_handoff);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_hangup_station(&f.ubus,
            f.ubus.runtime_id, "gate_main", media(&f)->generation));
        step(&f, 105); step(&f, 1105);
        TEST_ASSERT_INT_EQ(DF_GVS_ENDED, f.session.state);
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        df_runtime_media_module_stop(&f.media);
    }
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        struct call_fixture f;
        init(&f);
        if (scenario == 0) f.session.state = DF_GVS_PREVIEW;
        if (scenario == 1) f.session.generation = UINT64_MAX;
        if (scenario == 2) f.ubus.next_station_call_operation_id = UINT64_MAX;
        if (scenario == 3) f.control.dispatch.next_id = 0;
        if (scenario == 4) f.ubus.last_now_ms = UINT64_MAX - 10;
        if (scenario == 5) f.media.lifecycle_api = NULL;
        if (scenario == 6) f.control.dispatch.state = DF_GVS_CALL_QUEUED;
        uint64_t generation = 1;
        TEST_ASSERT_INT_EQ(1, df_runtime_ubus_call_station(&f.ubus,
            f.ubus.runtime_id, "gate_main", &generation) != DF_OK);
        TEST_ASSERT_INT_EQ(0, generation);
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        df_runtime_media_module_stop(&f.media);
    }
}

void test_runtime_station_call_global_hangup_and_failed_enqueue(void) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        struct call_fixture f;
        init(&f);
        if (scenario == 0) { admit(&f); confirm(&f); }
        else if (scenario == 1) talking(&f);
        else {
            const uint8_t ring[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 30};
            receive(&f, 1, ring, sizeof(ring), 0, 0x01020304, 100);
        }
        struct df_runtime_call_request request = {.type = DF_GVS_CALL_COMMAND_HANGUP,
            .runtime_id = "1111111111111111", .session_generation = f.session.generation,
            .reason = 1};
        if (scenario == 2) f.transport = DF_GVS_SEND_ATTEMPT_FAILURE;
        if (scenario == 3) {
            TEST_ASSERT_INT_EQ(DF_OK, df_gvs_call_control_submit_answer(&f.control,
                &f.session, f.session.generation, local, 8303, 8302, 120, 101));
            step(&f, 101);
            TEST_ASSERT_INT_EQ(DF_GVS_CALL_ACK_WAITING, f.control.acknowledgement.state);
        }
        if (scenario == 4) f.control.dispatch.next_id = 0;
        TEST_ASSERT_INT_EQ(scenario == 4 ? DF_ERR_IO : DF_OK,
            df_runtime_ubus_submit_call(&f.ubus, &request));
        step(&f, 105); step(&f, 605); step(&f, 1105); step(&f, 1605);
        TEST_ASSERT_INT_EQ(DF_GVS_ENDED, f.session.state);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_tick(&f.media, 10000));
        TEST_ASSERT_INT_EQ(0, ((struct df_media_session_manager *)f.media.instance)->active_count);
        if (scenario == 0) TEST_ASSERT_INT_EQ(0, f.calls);
        df_runtime_media_module_stop(&f.media);
    }
    struct call_fixture f;
    init(&f);
    uint64_t generation;
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_request_start(&f.media,
        "gate_main", 99, &generation));
    admit(&f);
    f.control.dispatch.next_id = 0; /* Exhaustion after admission, before promotion. */
    const uint8_t ack[] = {0x1e, 0, 1};
    receive(&f, 0x84, ack, sizeof(ack), 0, 0x01020304, 102);
    TEST_ASSERT_INT_EQ(DF_RUNTIME_STATION_CALL_FAILED, f.ubus.station_call.stage);
    TEST_ASSERT_INT_EQ(DF_GVS_IDLE, f.session.state);
    TEST_ASSERT_INT_EQ(40, f.session.generation);
    TEST_ASSERT_INT_EQ(1, media(&f)->active);
    TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, media(&f)->purpose);
    TEST_ASSERT_INT_EQ(generation, media(&f)->generation);
    TEST_ASSERT_INT_EQ(0, f.calls);
    df_runtime_media_module_stop(&f.media);
}

void test_runtime_station_call_direct_submit_and_ack_guards(void) {
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        struct call_fixture f;
        init(&f); admit(&f);
        const uint8_t ack[] = {0x1e, 0, 1};
        struct df_gvs_frame frame = {.family = 3, .opcode = 0x84,
            .payload = ack, .payload_length = sizeof(ack)};
        memcpy(frame.source, f.stations[0].logical_address, 6);
        memcpy(frame.destination, local, 6);
        TEST_ASSERT_INT_EQ(DF_OK, df_runtime_media_module_receive_control(&f.media,
            &frame, 0x01020304, 101));
        struct df_runtime_call_request request = {.type = DF_GVS_CALL_COMMAND_CALL,
            .runtime_id = "1111111111111111", .station_id = "gate_main",
            .session_generation = 41, .media_generation = f.ubus.station_call.media_generation,
            .primary_media_port = 8303, .secondary_media_port = 8302, .duration_seconds = 120};
        if (scenario == 0) request.runtime_id[0] = '2';
        if (scenario == 1) strcpy(request.station_id, "gate_side");
        if (scenario == 2) request.media_generation++;
        if (scenario == 3) request.session_generation = 500;
        if (scenario == 4) f.session.state = DF_GVS_RINGING;
        if (scenario == 5) request.primary_media_port = 0;
        if (scenario == 6) f.control.dispatch.next_id = 0;
        TEST_ASSERT_INT_EQ(1, df_runtime_call_submit(&request, 102, &f.binding) != DF_OK);
        TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_PREVIEW, media(&f)->purpose);
        TEST_ASSERT_INT_EQ(0, media(&f)->outbound_handoff);
        TEST_ASSERT_INT_EQ(40, f.session.generation);
        TEST_ASSERT_INT_EQ(0, f.calls);
        df_runtime_media_module_stop(&f.media);
    }
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        struct call_fixture f;
        init(&f); admit(&f); confirm(&f); step(&f, 103);
        uint8_t ack[] = {0, 0x20, 0x6f, 0, 0x20, 0x6e, 120};
        if (scenario == 0) ack[2]++;
        if (scenario == 1) f.session.generation++;
        receive(&f, 0x83, ack, scenario == 2 ? 6 : sizeof(ack), 0,
            0x01020304, scenario == 3 ? 1103 : 104);
        TEST_ASSERT_INT_EQ(1, f.session.state != DF_GVS_TALKING);
        TEST_ASSERT_INT_EQ(1, f.ubus.station_call.stage != DF_RUNTIME_STATION_CALL_TALKING);
        df_runtime_media_module_stop(&f.media);
    }
}
