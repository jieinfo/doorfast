#include "event_stream.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static const char *df_event_stream_valid_event(const char *event) {
    static const char *const names[] = {
        "incoming_call", "call_established", "hangup", "timeout", "preempted"
    };
    size_t index;

    if (event == NULL) {
        return NULL;
    }
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(event, names[index]) == 0) {
            return names[index];
        }
    }
    return NULL;
}

static void df_event_stream_client_clear(struct df_event_stream_client *client) {
    size_t index;

    if (client == NULL) {
        return;
    }
    if (client->fd >= 0) {
        (void)close(client->fd);
    }
    for (index = 0; index < DF_EVENT_STREAM_QUEUE_CAPACITY; index++) {
        free(client->queue[index]);
        client->queue[index] = NULL;
        client->queue_lengths[index] = 0;
    }
    client->fd = -1;
    client->queue_head = 0;
    client->queue_count = 0;
    client->queue_offset = 0;
}

static int df_event_stream_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int df_event_stream_client_add(struct df_event_stream *stream, int fd) {
    size_t index;

    for (index = 0; index < DF_EVENT_STREAM_MAX_CLIENTS; index++) {
        if (stream->clients[index].fd < 0) {
            stream->clients[index].fd = fd;
            return DF_OK;
        }
    }
    return DF_ERR_IO;
}

static void df_event_stream_drop_client(struct df_event_stream_client *client) {
    df_event_stream_client_clear(client);
}

int df_event_stream_init(struct df_event_stream *stream, const char *path) {
    struct sockaddr_un address = {0};
    struct stat existing;
    size_t path_length;
    int fd;

    if (stream == NULL || path == NULL || path[0] == '\0') {
        return DF_ERR_INVALID;
    }
    path_length = strlen(path);
    if (path_length >= sizeof(address.sun_path) || path_length >= sizeof(stream->path)) {
        return DF_ERR_INVALID;
    }
    memset(stream, 0, sizeof(*stream));
    stream->listen_fd = -1;
    for (size_t index = 0; index < DF_EVENT_STREAM_MAX_CLIENTS; index++) {
        stream->clients[index].fd = -1;
    }
    memcpy(stream->path, path, path_length + 1U);
    if (lstat(path, &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || unlink(path) != 0) {
            stream->path[0] = '\0';
            return DF_ERR_IO;
        }
    } else if (errno != ENOENT) {
        stream->path[0] = '\0';
        return DF_ERR_IO;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || df_event_stream_set_nonblocking(fd) != DF_OK) {
        if (fd >= 0) (void)close(fd);
        stream->path[0] = '\0';
        return DF_ERR_IO;
    }
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_length + 1U);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(path, 0660) != 0 || listen(fd, (int)DF_EVENT_STREAM_MAX_CLIENTS) != 0) {
        (void)close(fd);
        (void)unlink(path);
        stream->path[0] = '\0';
        return DF_ERR_IO;
    }
    {
        struct group *group = getgrnam("doorfast");
        if (group != NULL && chown(path, (uid_t)0, group->gr_gid) != 0) {
            (void)close(fd);
            (void)unlink(path);
            stream->path[0] = '\0';
            return DF_ERR_IO;
        }
#ifdef DF_WITH_UBUS
        if (group == NULL) {
            (void)close(fd);
            (void)unlink(path);
            stream->path[0] = '\0';
            return DF_ERR_IO;
        }
#endif
    }
    stream->listen_fd = fd;
    return DF_OK;
}

int df_event_stream_publish(struct df_event_stream *stream, const char *event,
                            uint64_t generation, uint64_t now_ms) {
    char line[DF_EVENT_STREAM_EVENT_MAX];
    size_t line_length;
    size_t index;

    if (stream == NULL || stream->listen_fd < 0 ||
        df_event_stream_valid_event(event) == NULL || generation == 0U ||
        now_ms < stream->last_timestamp_ms || stream->next_event_id == UINT64_MAX) {
        return DF_ERR_INVALID;
    }
    line_length = (size_t)snprintf(line, sizeof(line),
        "{\"schema_version\":1,\"event_id\":%llu,\"event\":\"%s\","
        "\"generation\":%llu,\"timestamp_ms\":%llu}\n",
        (unsigned long long)(stream->next_event_id + 1U), event,
        (unsigned long long)generation, (unsigned long long)now_ms);
    if (line_length == 0U || line_length >= sizeof(line)) {
        return DF_ERR_IO;
    }
    stream->next_event_id++;
    stream->last_timestamp_ms = now_ms;
    for (index = 0; index < DF_EVENT_STREAM_MAX_CLIENTS; index++) {
        struct df_event_stream_client *client = &stream->clients[index];
        size_t slot;
        char *copy;

        if (client->fd < 0) {
            continue;
        }
        if (client->queue_count >= DF_EVENT_STREAM_QUEUE_CAPACITY) {
            df_event_stream_drop_client(client);
            continue;
        }
        copy = malloc(line_length);
        if (copy == NULL) {
            df_event_stream_drop_client(client);
            continue;
        }
        memcpy(copy, line, line_length);
        slot = (client->queue_head + client->queue_count) %
            DF_EVENT_STREAM_QUEUE_CAPACITY;
        client->queue[slot] = copy;
        client->queue_lengths[slot] = line_length;
        client->queue_count++;
    }
    return DF_OK;
}

static void df_event_stream_accept(struct df_event_stream *stream) {
    for (;;) {
        int client_fd = accept(stream->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
        if (df_event_stream_set_nonblocking(client_fd) != DF_OK ||
            df_event_stream_client_add(stream, client_fd) != DF_OK) {
            (void)close(client_fd);
        }
    }
}

static void df_event_stream_flush_client(struct df_event_stream_client *client) {
    while (client->queue_count > 0U) {
        size_t slot = client->queue_head;
        size_t remaining = client->queue_lengths[slot] - client->queue_offset;
        ssize_t written = send(client->fd, client->queue[slot] + client->queue_offset,
                               remaining, MSG_NOSIGNAL);

        if (written > 0) {
            client->queue_offset += (size_t)written;
            if (client->queue_offset == client->queue_lengths[slot]) {
                free(client->queue[slot]);
                client->queue[slot] = NULL;
                client->queue_lengths[slot] = 0;
                client->queue_head = (client->queue_head + 1U) %
                    DF_EVENT_STREAM_QUEUE_CAPACITY;
                client->queue_count--;
                client->queue_offset = 0;
            }
            continue;
        }
        if (written < 0 && (errno == EINTR)) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        df_event_stream_drop_client(client);
        return;
    }
}

int df_event_stream_process(struct df_event_stream *stream) {
    size_t index;

    if (stream == NULL) {
        return DF_ERR_INVALID;
    }
    if (stream->listen_fd < 0) {
        return DF_OK;
    }
    df_event_stream_accept(stream);
    for (index = 0; index < DF_EVENT_STREAM_MAX_CLIENTS; index++) {
        if (stream->clients[index].fd >= 0) {
            df_event_stream_flush_client(&stream->clients[index]);
        }
    }
    return DF_OK;
}

void df_event_stream_stop(struct df_event_stream *stream) {
    size_t index;

    if (stream == NULL) {
        return;
    }
    if (stream->path[0] == '\0') {
        return;
    }
    for (index = 0; index < DF_EVENT_STREAM_MAX_CLIENTS; index++) {
        df_event_stream_client_clear(&stream->clients[index]);
    }
    if (stream->listen_fd >= 0) {
        (void)close(stream->listen_fd);
        stream->listen_fd = -1;
    }
    if (stream->path[0] != '\0') {
        (void)unlink(stream->path);
    }
    stream->path[0] = '\0';
}
