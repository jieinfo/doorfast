#ifndef DOORFAST_EVENT_STREAM_H
#define DOORFAST_EVENT_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_EVENT_STREAM_DEFAULT_PATH "/var/run/doorfast/events.sock"
#define DF_EVENT_STREAM_MAX_CLIENTS 16U
#define DF_EVENT_STREAM_QUEUE_CAPACITY 64U
#define DF_EVENT_STREAM_EVENT_MAX 256U

struct df_event_stream_client {
    int fd;
    char *queue[DF_EVENT_STREAM_QUEUE_CAPACITY];
    size_t queue_lengths[DF_EVENT_STREAM_QUEUE_CAPACITY];
    size_t queue_head;
    size_t queue_count;
    size_t queue_offset;
};

struct df_event_stream {
    int listen_fd;
    char path[108];
    uint64_t next_event_id;
    uint64_t last_timestamp_ms;
    struct df_event_stream_client clients[DF_EVENT_STREAM_MAX_CLIENTS];
};

int df_event_stream_init(struct df_event_stream *, const char *path);
int df_event_stream_publish(struct df_event_stream *, const char *event,
                            uint64_t generation, uint64_t now_ms);
int df_event_stream_publish_station(struct df_event_stream *, const char *event,
                                    const char *station_id,
                                    uint64_t generation, uint64_t now_ms);
int df_event_stream_publish_logical_address(struct df_event_stream *,
    const char *event, const uint8_t logical_address[6],
    uint64_t generation, uint64_t now_ms);
int df_event_stream_process(struct df_event_stream *);
void df_event_stream_stop(struct df_event_stream *);

#endif
