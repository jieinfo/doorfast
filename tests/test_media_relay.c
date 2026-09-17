#include <stdio.h>
#include <string.h>

#include "media_relay.h"
#include "test.h"

struct relay_capture {
    unsigned calls;
    char url[DF_MEDIA_RELAY_URL_MAX];
    char token[DF_MEDIA_RELAY_TOKEN_MAX];
    char json[DF_MEDIA_RELAY_JSON_MAX];
};

static int relay_capture_send(const char *url, const char *token,
    const char *json, void *context) {
    struct relay_capture *capture = context;
    capture->calls++;
    (void)snprintf(capture->url, sizeof(capture->url), "%s", url);
    (void)snprintf(capture->token, sizeof(capture->token), "%s", token);
    (void)snprintf(capture->json, sizeof(capture->json), "%s", json);
    return DF_OK;
}

static int relay_fail_send(const char *url, const char *token,
    const char *json, void *context) {
    (void)url;
    (void)token;
    (void)json;
    (void)context;
    return DF_ERR_IO;
}

void test_media_relay_accepts_http_and_redacts_event_payload(void) {
    struct relay_capture capture = {0};
    struct df_media_relay relay = {0};
    const struct df_media_relay_event event = {
        .name = "monitor_publishing",
        .generation = 7,
        .status_revision = 3,
        .timestamp_ms = 100,
        .state = "publishing",
        .encoder_running = true,
        .queue_drops = 2,
        .relay_failures = 1,
        .failure = "",
    };

    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_init(&relay,
        "http://ha.local:8123", "relay-secret", relay_capture_send,
        &capture));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_enqueue(&relay, &event));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_tick(&relay, 100));
    TEST_ASSERT_INT_EQ(1, (int)capture.calls);
    TEST_ASSERT_INT_EQ(0, strcmp("http://ha.local:8123", capture.url));
    TEST_ASSERT_INT_EQ(0, strcmp("relay-secret", capture.token));
    TEST_ASSERT_INT_EQ(1, strstr(capture.json, "monitor_publishing") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(capture.json, "generation") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(capture.json,
        "\"status\":{\"state\":\"publishing\"") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(capture.json,
        "\"encoder_running\":true") != NULL);
    TEST_ASSERT_INT_EQ(1, strstr(capture.json,
        "\"queue_drops\":2") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(capture.json, "relay-secret") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(capture.json, "rtsp://") != NULL);
    TEST_ASSERT_INT_EQ(0, strstr(capture.json, "payload") != NULL);
    TEST_ASSERT_INT_EQ(0, (int)df_media_relay_pending(&relay));
}

void test_media_relay_retries_bounded_and_rejects_other_schemes(void) {
    struct df_media_relay relay = {0};
    struct df_media_relay_event event = {
        .name = "monitor_failed", .generation = 9,
        .status_revision = 4, .timestamp_ms = 200, .state = "failed",
    };

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_media_relay_init(&relay,
        "ftp://ha.local", "token", relay_fail_send, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_init(&relay,
        "https://ha.local", "token", relay_fail_send, NULL));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_enqueue(&relay, &event));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_tick(&relay, 200));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_relay_tick(&relay, 1200));
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_media_relay_tick(&relay, 3200));
    TEST_ASSERT_INT_EQ(0, (int)df_media_relay_pending(&relay));
    TEST_ASSERT_INT_EQ(1, (int)df_media_relay_failed(&relay));
    df_media_relay_destroy(&relay);
}
