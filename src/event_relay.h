#ifndef DOORFAST_EVENT_RELAY_H
#define DOORFAST_EVENT_RELAY_H

#include <stddef.h>
#include <stdint.h>

#define DF_RELAY_MAX_LINE 2048U
#define DF_RELAY_QUEUE_CAPACITY 64U

struct df_relay_event {
    char line[DF_RELAY_MAX_LINE];
    uint64_t event_id;
    uint64_t generation;
    uint64_t timestamp_ms;
};

struct df_relay_queue {
    struct df_relay_event entries[DF_RELAY_QUEUE_CAPACITY];
    size_t head;
    size_t length;
    unsigned long dropped;
};

int df_relay_parse_event(const char *line, size_t length,
                         struct df_relay_event *event);
void df_relay_queue_init(struct df_relay_queue *queue);
int df_relay_queue_push(struct df_relay_queue *queue,
                        const struct df_relay_event *event);
int df_relay_queue_peek(const struct df_relay_queue *queue,
                        struct df_relay_event *event);
int df_relay_queue_pop(struct df_relay_queue *queue,
                       struct df_relay_event *event);
int df_relay_retryable_status(int status_code);
uint64_t df_relay_backoff_ms(unsigned attempt, uint64_t max_ms);
int df_relay_token_file_ok(const char *path);
int df_relay_validate_https_url(const char *url);
int df_relay_validate_entry_id(const char *entry_id);

#endif
