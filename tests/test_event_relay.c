#include "event_relay.h"
#include "test.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_parse(void) {
    const char *line = "{\"schema_version\":1,\"event_id\":7,\"event\":\"incoming_call\",\"generation\":3,\"timestamp_ms\":42}\n";
    struct df_relay_event event;
    TEST_ASSERT_INT_EQ(0, df_relay_parse_event(line, strlen(line), &event));
    TEST_ASSERT_INT_EQ(7, (int)event.event_id);
    TEST_ASSERT_INT_EQ(3, (int)event.generation);
    TEST_ASSERT_INT_EQ(0, event.station_id[0]);
    line = "{\"schema_version\":1,\"event_id\":8,"
        "\"event\":\"media_pipeline_failed\","
        "\"station_id\":\"gate_side\",\"generation\":4,\"timestamp_ms\":43}\n";
    TEST_ASSERT_INT_EQ(0, df_relay_parse_event(line, strlen(line), &event));
    TEST_ASSERT_INT_EQ(0, strcmp("gate_side", event.station_id));
    line = "{\"schema_version\":1,\"event_id\":9,\"event\":\"incoming_call\","
        "\"logical_address\":\"32:02:01:00:09:00\",\"generation\":5,"
        "\"timestamp_ms\":44}\n";
    TEST_ASSERT_INT_EQ(0, df_relay_parse_event(line, strlen(line), &event));
    TEST_ASSERT_INT_EQ(0, strcmp("32:02:01:00:09:00",
        event.logical_address));
    TEST_ASSERT_INT_EQ(-1, df_relay_parse_event("{\"schema_version\":2}", 20, &event));
}

static void test_queue(void) {
    struct df_relay_queue queue;
    struct df_relay_event event;
    size_t i;
    df_relay_queue_init(&queue);
    for (i = 1; i <= DF_RELAY_QUEUE_CAPACITY + 2; i++) {
        memset(&event, 0, sizeof(event));
        event.event_id = i;
        TEST_ASSERT_INT_EQ(0, df_relay_queue_push(&queue, &event));
    }
    TEST_ASSERT_INT_EQ(DF_RELAY_QUEUE_CAPACITY, (int)queue.length);
    TEST_ASSERT_INT_EQ(2, (int)queue.dropped);
    TEST_ASSERT_INT_EQ(0, df_relay_queue_peek(&queue, &event));
    TEST_ASSERT_INT_EQ(3, (int)event.event_id);
}

static void test_policy(void) {
    TEST_ASSERT_INT_EQ(1, df_relay_retryable_status(503));
    TEST_ASSERT_INT_EQ(0, df_relay_retryable_status(401));
    TEST_ASSERT_INT_EQ(4000, (int)df_relay_backoff_ms(2, 10000));
    TEST_ASSERT_INT_EQ(10000, (int)df_relay_backoff_ms(8, 10000));
    TEST_ASSERT_INT_EQ(0, df_relay_validate_url("https://example"));
    TEST_ASSERT_INT_EQ(0, df_relay_validate_url("https://example:8443"));
    TEST_ASSERT_INT_EQ(0, df_relay_validate_url("http://example"));
    TEST_ASSERT_INT_EQ(0, df_relay_validate_url("http://example:8123"));
    TEST_ASSERT_INT_EQ(-1, df_relay_validate_url("https://example/base"));
    TEST_ASSERT_INT_EQ(-1, df_relay_validate_url("https://example\r\nX"));
    TEST_ASSERT_INT_EQ(-1, df_relay_validate_url("ftp://example"));
    TEST_ASSERT_INT_EQ(0, df_relay_validate_entry_id("entry_1"));
    TEST_ASSERT_INT_EQ(-1, df_relay_validate_entry_id("entry/1"));
}

void test_event_relay(void) {
    test_parse();
    test_queue();
    test_policy();
}
