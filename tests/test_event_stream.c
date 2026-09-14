#include "test.h"

#include "event_stream.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void event_stream_path(char path[108]) {
    (void)snprintf(path, 108, "/tmp/doorfast-events-%ld.sock", (long)getpid());
    (void)unlink(path);
}

static int event_stream_connect(const char *path) {
    struct sockaddr_un address = {0};
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        return -1;
    }
    address.sun_family = AF_UNIX;
    (void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

void test_event_stream_serializes_exact_json(void) {
    struct df_event_stream stream;
    char path[108];
    char line[256] = {0};
    int client;
    ssize_t length;

    event_stream_path(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_init(&stream, path));
    client = event_stream_connect(path);
    TEST_ASSERT_INT_EQ(1, client >= 0);
    if (client >= 0) {
        (void)fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) | O_NONBLOCK);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_event_stream_publish(&stream, "incoming_call", 7, 42));
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    length = read(client, line, sizeof(line) - 1U);
    TEST_ASSERT_INT_EQ(91, (int)length);
    if (length > 0) {
        line[length] = '\0';
        TEST_ASSERT_INT_EQ(0, strcmp(line,
            "{\"schema_version\":1,\"event_id\":1,\"event\":\"incoming_call\",\"generation\":7,\"timestamp_ms\":42}\n"));
    }
    (void)close(client);
    df_event_stream_stop(&stream);
    (void)unlink(path);
}

void test_event_stream_rejects_unknown_event(void) {
    struct df_event_stream stream;
    char path[108];

    event_stream_path(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_init(&stream, path));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_event_stream_publish(&stream, "bogus", 1, 1));
    df_event_stream_stop(&stream);
    (void)unlink(path);
}

void test_event_stream_bounds_each_client_at_64_events(void) {
    struct df_event_stream stream;
    char path[108];
    int client;
    unsigned index;
    char line[256];
    unsigned received = 0;

    event_stream_path(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_init(&stream, path));
    client = event_stream_connect(path);
    TEST_ASSERT_INT_EQ(1, client >= 0);
    if (client >= 0) {
        (void)fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) | O_NONBLOCK);
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    for (index = 0; index < 64U; index++) {
        TEST_ASSERT_INT_EQ(DF_OK,
            df_event_stream_publish(&stream, "timeout", index + 1U, index + 1U));
    }
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    for (;;) {
        ssize_t length = read(client, line, sizeof(line));
        ssize_t offset;
        if (length <= 0) {
            break;
        }
        for (offset = 0; offset < length; offset++) {
            if (line[offset] == '\n') {
                received++;
            }
        }
    }
    TEST_ASSERT_INT_EQ(64, (int)received);
    (void)close(client);
    df_event_stream_stop(&stream);
    (void)unlink(path);
}

void test_event_stream_drops_disconnected_client_without_error(void) {
    struct df_event_stream stream;
    char path[108];
    int client;

    event_stream_path(path);
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_init(&stream, path));
    client = event_stream_connect(path);
    TEST_ASSERT_INT_EQ(1, client >= 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    (void)close(client);
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_event_stream_publish(&stream, "hangup", 1, 1));
    TEST_ASSERT_INT_EQ(DF_OK, df_event_stream_process(&stream));
    df_event_stream_stop(&stream);
    (void)unlink(path);
}
