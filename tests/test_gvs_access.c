#include "test.h"

#include <string.h>

#include "gvs_access.h"
#include "gvs_memory_sender.h"

static struct df_gvs_session access_session(void) {
    struct df_gvs_session session = {
        .state = DF_GVS_TALKING,
        .generation = 11,
        .peer = {0x32, 0x02, 0x01, 0x00, 0x01, 0x00},
    };
    return session;
}

void test_gvs_access_serializes_vendor_direct_unlock_shape(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t material[8] = {
        0x0d, 0x75, 0x3e, 0xa9, 0x90, 0x03, 0xcd, 0x5d,
    };
    struct df_gvs_session session = access_session();
    struct df_gvs_access_request request;
    uint8_t frame[DF_GVS_ACCESS_DIRECT_FRAME_SIZE];
    size_t length = 0;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_prepare_direct(
        &session, 11, local, material, &request));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_serialize_direct(
        &request, frame, sizeof(frame), &length,
        df_gvs_placeholder_header_fields, NULL));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_DIRECT_FRAME_SIZE, (int)length);
    TEST_ASSERT_INT_EQ(0x04, frame[38]);
    TEST_ASSERT_INT_EQ(0x09, frame[39]);
    TEST_ASSERT_INT_EQ(0x0c, frame[40]);
    TEST_ASSERT_INT_EQ(0x00, frame[41]);
    TEST_ASSERT_INT_EQ(0, memcmp(frame + 42, material, sizeof(material)));
}

void test_gvs_access_rejects_stale_session_and_invalid_target(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    const uint8_t material[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    struct df_gvs_session session = access_session();
    struct df_gvs_access_request request;

    memset(&request, 0xa5, sizeof(request));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_prepare_direct(
        &session, 10, local, material, &request));
    TEST_ASSERT_INT_EQ(0, request.valid);
    session.peer[0] = 0x35;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_prepare_direct(
        &session, 11, local, material, &request));
    session.peer[0] = 0x32;
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_prepare_direct(
        &session, 11, local, material, &request));
}

void test_gvs_access_result_tracks_protocol_reply_without_physical_claim(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    struct df_gvs_session session = access_session();
    struct df_gvs_access_result result;
    struct df_gvs_frame frame = {
        .family = 0x04,
        .opcode = 0x89,
    };
    uint8_t status = 0x01;

    memcpy(frame.source, session.peer, 6);
    memcpy(frame.destination, local, 6);
    frame.payload = &status;
    frame.payload_length = 1;
    df_gvs_access_result_init(&result, 100);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_begin(
        &result, &session, 11, local, 100, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_observe(
        &result, &session, local, &frame, 200));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_COMPLETED, result.state);
    TEST_ASSERT_INT_EQ(0x01, result.raw_status);
    TEST_ASSERT_INT_EQ(0, result.physical_result_confirmed);
}

void test_gvs_access_result_rejects_failure_and_expires_unanswered(void) {
    const uint8_t local[6] = {0x61, 0x02, 0x01, 0x01, 0x01, 0x01};
    struct df_gvs_session session = access_session();
    struct df_gvs_access_result result;
    struct df_gvs_frame frame = {
        .family = 0x04,
        .opcode = 0x89,
    };
    uint8_t status = 0x7f;

    memcpy(frame.source, session.peer, 6);
    memcpy(frame.destination, local, 6);
    frame.payload = &status;
    frame.payload_length = 1;
    df_gvs_access_result_init(&result, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_begin(
        &result, &session, 11, local, 0, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_observe(
        &result, &session, local, &frame, 500));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_REJECTED, result.state);
    TEST_ASSERT_INT_EQ(0x7f, result.raw_status);

    df_gvs_access_result_init(&result, 1000);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_begin(
        &result, &session, 11, local, 1000, 1000));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(
        &result, &session, local, 2000));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_EXPIRED, result.state);
}

static int access_send_count(const struct df_gvs_access_request *request,
                             void *context) {
    int *count = context;
    TEST_ASSERT_INT_EQ(1, request->valid);
    (*count)++;
    return DF_OK;
}

void test_gvs_access_control_sends_once_and_requires_material(void) {
    struct df_gvs_access_control control;
    struct df_gvs_session session = access_session();
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    int sent = 0;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &control, "", 0, access_send_count, &sent));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_submit(
        &control, &session, 11, local, 0));
    TEST_ASSERT_INT_EQ(0, sent);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_init(
        &control, "00112233445566xz", 0, access_send_count, &sent));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(
        &control, "0011223344556677", 0, access_send_count, &sent));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_submit(
        &control, &session, 10, local, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_submit(
        &control, &session, 11, local, 1));
    TEST_ASSERT_INT_EQ(1, sent);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_submit(
        &control, &session, 11, local, 2));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(
        &control.result, &session, local, 1001));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_EXPIRED, control.result.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_submit(
        &control, &session, 11, local, 1002));
    TEST_ASSERT_INT_EQ(1, sent);
    session.generation++;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_submit(
        &control, &session, 12, local, 1003));
    TEST_ASSERT_INT_EQ(2, sent);
}

static int access_send_fail(const struct df_gvs_access_request *request,
                            void *context) {
    (void)request;
    (*(int *)context)++;
    return DF_ERR_IO;
}

void test_gvs_access_failed_send_and_late_reply(void) {
    struct df_gvs_access_control control;
    struct df_gvs_session session = access_session();
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    uint8_t status = 1;
    struct df_gvs_frame frame = {.family = 4, .opcode = 0x89,
        .payload = &status, .payload_length = 1};
    int sent = 0;
    memcpy(frame.source, session.peer, 6);
    memcpy(frame.destination, local, 6);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(&control,
        "0011223344556677", 0, access_send_fail, &sent));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_access_control_submit(
        &control, &session, 11, local, 1));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_SEND_FAILED, control.result.state);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_result_observe(
        &control.result, &session, local, &frame, 2));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_control_submit(
        &control, &session, 11, local, 3));
    TEST_ASSERT_INT_EQ(1, sent);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_init(&control,
        "0011223344556677", 0, access_send_count, &sent));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_control_submit(
        &control, &session, 11, local, 1));
    frame.source[1]++;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_result_observe(
        &control.result, &session, local, &frame, 2));
    frame.source[1]--;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_access_result_observe(
        &control.result, &session, local, &frame, 1001));
    session.state = DF_GVS_ENDED;
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_access_result_tick(
        &control.result, &session, local, 1001));
    TEST_ASSERT_INT_EQ(DF_GVS_ACCESS_PROTOCOL_CANCELLED, control.result.state);
}
