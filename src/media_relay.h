#ifndef DOORFAST_MEDIA_RELAY_H
#define DOORFAST_MEDIA_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"

#define DF_MEDIA_RELAY_QUEUE_CAPACITY 8U
#define DF_MEDIA_RELAY_URL_MAX 256U
#define DF_MEDIA_RELAY_TOKEN_MAX 256U
#define DF_MEDIA_RELAY_EVENT_NAME_MAX 32U
#define DF_MEDIA_RELAY_STATE_MAX 32U
#define DF_MEDIA_RELAY_JSON_MAX 1024U

struct df_media_relay_event {
    const char *name;
    uint64_t generation;
    uint64_t status_revision;
    uint64_t timestamp_ms;
    const char *state;
    bool encoder_running;
    unsigned queue_drops;
    unsigned relay_failures;
    const char *failure;
};

typedef int (*df_media_relay_send_fn)(const char *url, const char *token,
    const char *json, void *context);

struct df_media_relay_entry {
    bool valid;
    unsigned attempts;
    uint64_t next_attempt_ms;
    char json[DF_MEDIA_RELAY_JSON_MAX];
};

struct df_media_relay {
    char url[DF_MEDIA_RELAY_URL_MAX];
    char token[DF_MEDIA_RELAY_TOKEN_MAX];
    struct df_media_relay_entry entries[DF_MEDIA_RELAY_QUEUE_CAPACITY];
    size_t read;
    size_t write;
    size_t count;
    unsigned dropped;
    unsigned failed;
    uint64_t next_event_id;
    df_media_relay_send_fn send;
    void *send_context;
    void *curl_multi;
    void *curl_easy;
    void *curl_headers;
};

int df_media_relay_init(struct df_media_relay *, const char *url,
    const char *token, df_media_relay_send_fn, void *context);
int df_media_relay_enqueue(struct df_media_relay *,
    const struct df_media_relay_event *);
int df_media_relay_tick(struct df_media_relay *, uint64_t now_ms);
size_t df_media_relay_pending(const struct df_media_relay *);
unsigned df_media_relay_failed(const struct df_media_relay *);
void df_media_relay_destroy(struct df_media_relay *);

#endif
