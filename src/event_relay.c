#include "event_relay.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int field_u64(const char *line, const char *name, uint64_t *out) {
    char key[64];
    const char *p;
    char *end;
    int count;
    if (snprintf(key, sizeof(key), "\"%s\":", name) >= (int)sizeof(key)) return -1;
    p = strstr(line, key);
    if (!p) return -1;
    p += strlen(key);
    errno = 0;
    *out = strtoull(p, &end, 10);
    if (errno || end == p || (*end != ',' && *end != '}')) return -1;
    count = (int)(end - p);
    return count > 0 ? 0 : -1;
}

static int field_string(const char *line, const char *name, char *out, size_t out_size) {
    char key[64];
    const char *p;
    const char *end;
    size_t length;
    if (snprintf(key, sizeof(key), "\"%s\":\"", name) >= (int)sizeof(key)) return -1;
    p = strstr(line, key);
    if (!p) return -1;
    p += strlen(key);
    end = strchr(p, '\"');
    if (!end || end == p) return -1;
    length = (size_t)(end - p);
    if (length + 1 > out_size) return -1;
    memcpy(out, p, length);
    out[length] = '\0';
    return 0;
}

int df_relay_parse_event(const char *line, size_t length,
                         struct df_relay_event *event) {
    char name[32];
    uint64_t schema;
    if (!line || !event || length == 0 || length >= DF_RELAY_MAX_LINE) return -1;
    if (line[length - 1] == '\n') length--;
    if (length == 0 || line[length - 1] != '}') return -1;
    memset(event, 0, sizeof(*event));
    memcpy(event->line, line, length);
    event->line[length] = '\0';
    if (field_u64(event->line, "schema_version", &schema) || schema != 1 ||
        field_string(event->line, "event", name, sizeof(name)) ||
        (strcmp(name, "incoming_call") && strcmp(name, "call_established") &&
         strcmp(name, "hangup") && strcmp(name, "timeout") && strcmp(name, "preempted")) ||
        field_u64(event->line, "event_id", &event->event_id) || event->event_id == 0 ||
        field_u64(event->line, "generation", &event->generation) || event->generation == 0 ||
        field_u64(event->line, "timestamp_ms", &event->timestamp_ms) || event->timestamp_ms == 0) {
        return -1;
    }
    return 0;
}

void df_relay_queue_init(struct df_relay_queue *queue) {
    memset(queue, 0, sizeof(*queue));
}

int df_relay_queue_push(struct df_relay_queue *queue,
                        const struct df_relay_event *event) {
    size_t index;
    if (!queue || !event) return -1;
    if (queue->length == DF_RELAY_QUEUE_CAPACITY) {
        queue->head = (queue->head + 1) % DF_RELAY_QUEUE_CAPACITY;
        queue->length--;
        queue->dropped++;
    }
    index = (queue->head + queue->length) % DF_RELAY_QUEUE_CAPACITY;
    queue->entries[index] = *event;
    queue->length++;
    return 0;
}

int df_relay_queue_peek(const struct df_relay_queue *queue,
                        struct df_relay_event *event) {
    if (!queue || !event || queue->length == 0) return -1;
    *event = queue->entries[queue->head];
    return 0;
}

int df_relay_queue_pop(struct df_relay_queue *queue,
                       struct df_relay_event *event) {
    if (df_relay_queue_peek(queue, event)) return -1;
    queue->head = (queue->head + 1) % DF_RELAY_QUEUE_CAPACITY;
    queue->length--;
    return 0;
}

int df_relay_retryable_status(int status_code) {
    return status_code == 408 || status_code == 425 || status_code == 429 || status_code >= 500;
}

uint64_t df_relay_backoff_ms(unsigned attempt, uint64_t max_ms) {
    uint64_t value = 1000;
    unsigned i;
    if (max_ms == 0) return 0;
    for (i = 0; i < attempt && value < max_ms; i++) {
        if (value > max_ms / 2) value = max_ms;
        else value *= 2;
    }
    return value > max_ms ? max_ms : value;
}

int df_relay_token_file_ok(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) || !S_ISREG(st.st_mode)) return -1;
    if (st.st_uid != 0 || (st.st_mode & 0077) != 0 || st.st_size == 0 || st.st_size > 4096) return -1;
    return 0;
}

int df_relay_validate_https_url(const char *url) {
    if (!url || strncmp(url, "https://", 8) != 0 || url[8] == '\0' || strchr(url + 8, ' ')) return -1;
    return 0;
}

int df_relay_validate_entry_id(const char *entry_id) {
    size_t i;
    if (!entry_id || !entry_id[0] || strlen(entry_id) > 64) return -1;
    for (i = 0; entry_id[i]; i++) {
        unsigned char c = (unsigned char)entry_id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return -1;
    }
    return 0;
}
