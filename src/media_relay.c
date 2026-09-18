#include "media_relay.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

static bool df_media_relay_url_valid(const char *url) {
    const char *authority;
    const char *path;
    size_t length;
    size_t authority_length;
    size_t index;

    if (url == NULL) return false;
    if (url[0] == '\0') return true;
    if (strncmp(url, "http://", 7U) == 0) authority = url + 7U;
    else if (strncmp(url, "https://", 8U) == 0) authority = url + 8U;
    else return false;
    length = strlen(authority);
    if (length == 0U || length >= DF_MEDIA_RELAY_URL_MAX) return false;
    path = strchr(authority, '/');
    authority_length = path == NULL ? length : (size_t)(path - authority);
    if (authority_length == 0U) return false;
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)authority[index];
        if (value < 0x21U || value > 0x7eU || value == '?' ||
            value == '#' || value == '@') return false;
    }
    return true;
}

static bool df_media_relay_token_valid(const char *token) {
    size_t length;
    size_t index;

    if (token == NULL) return false;
    length = strlen(token);
    if (length >= DF_MEDIA_RELAY_TOKEN_MAX) return false;
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)token[index];
        if (value < 0x21U || value > 0x7eU || value == '\r' || value == '\n')
            return false;
    }
    return true;
}

static const char *df_media_relay_event_name(const char *name) {
    static const char *const names[] = {
        "monitor_requested", "monitor_confirmed", "monitor_media_ready",
        "monitor_publishing", "monitor_failed", "monitor_stopped",
        "monitor_preempted",
    };
    size_t index;

    if (name == NULL) return NULL;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(name, names[index]) == 0) return names[index];
    }
    return NULL;
}

static const char *df_media_relay_state_name(const char *state) {
    static const char *const states[] = {
        "idle", "requesting", "awaiting_video", "publishing", "viewing",
        "stopping", "failed",
    };
    size_t index;

    if (state == NULL) return NULL;
    for (index = 0U; index < sizeof(states) / sizeof(states[0]); index++) {
        if (strcmp(state, states[index]) == 0) return states[index];
    }
    return NULL;
}

static const char *df_media_relay_failure_name(const char *failure) {
    static const char *const failures[] = {
        "", "capacity_exhausted", "monitor_timeout", "monitor_unconfirmed",
        "first_frame_timeout", "source_mismatch", "encoder_unavailable",
        "encoder_exited", "rtsp_publish_failed", "call_preempted",
        "stop_timeout", "control_send_failed", "monitor_failed",
    };
    size_t index;

    if (failure == NULL) return NULL;
    for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); index++) {
        if (strcmp(failure, failures[index]) == 0) return failures[index];
    }
    return NULL;
}

static void df_media_relay_clear(void *memory, size_t length) {
    volatile unsigned char *bytes = memory;
    while (length-- > 0U) *bytes++ = 0U;
}

static size_t df_media_relay_discard(char *data, size_t size, size_t count,
    void *context) {
    (void)data;
    (void)context;
    return size * count;
}

static void df_media_relay_curl_cleanup_request(struct df_media_relay *relay) {
    if (relay->curl_easy != NULL) {
        if (relay->curl_multi != NULL)
            (void)curl_multi_remove_handle(relay->curl_multi, relay->curl_easy);
        curl_easy_cleanup(relay->curl_easy);
        relay->curl_easy = NULL;
    }
    if (relay->curl_headers != NULL) {
        curl_slist_free_all(relay->curl_headers);
        relay->curl_headers = NULL;
    }
}

static void df_media_relay_pop(struct df_media_relay *relay) {
    if (relay->count == 0U) return;
    memset(&relay->entries[relay->read], 0, sizeof(relay->entries[relay->read]));
    relay->read = (relay->read + 1U) % DF_MEDIA_RELAY_QUEUE_CAPACITY;
    relay->count--;
}

int df_media_relay_init(struct df_media_relay *relay, const char *url,
    const char *token, df_media_relay_send_fn send, void *context) {
    if (relay == NULL || !df_media_relay_url_valid(url) ||
        !df_media_relay_token_valid(token) ||
        (url[0] != '\0' && token[0] == '\0')) {
        return DF_ERR_INVALID;
    }
    memset(relay, 0, sizeof(*relay));
    (void)snprintf(relay->url, sizeof(relay->url), "%s", url);
    (void)snprintf(relay->token, sizeof(relay->token), "%s", token);
    relay->send = send;
    relay->send_context = context;
    relay->next_event_id = 1U;
    if (url[0] != '\0' && send == NULL) {
        relay->curl_multi = curl_multi_init();
        if (relay->curl_multi == NULL) {
            df_media_relay_destroy(relay);
            return DF_ERR_IO;
        }
    }
    return DF_OK;
}

int df_media_relay_enqueue(struct df_media_relay *relay,
    const struct df_media_relay_event *event) {
    struct df_media_relay_entry *entry;
    const char *name;
    const char *state;
    const char *failure;
    int written;

    if (relay == NULL || event == NULL || event->generation == 0U ||
        event->status_revision == 0U || event->name == NULL ||
        event->state == NULL || relay->next_event_id == 0U) {
        return DF_ERR_INVALID;
    }
    name = df_media_relay_event_name(event->name);
    state = df_media_relay_state_name(event->state);
    failure = df_media_relay_failure_name(event->failure == NULL ? "" :
        event->failure);
    if (name == NULL || state == NULL || failure == NULL) return DF_ERR_INVALID;
    if (relay->url[0] == '\0') return DF_OK;
    if (relay->count == DF_MEDIA_RELAY_QUEUE_CAPACITY) {
        df_media_relay_pop(relay);
        if (relay->dropped < UINT_MAX) relay->dropped++;
    }
    entry = &relay->entries[relay->write];
    written = snprintf(entry->json, sizeof(entry->json),
        "{\"schema_version\":1,\"event_id\":%llu,"
        "\"generation\":%llu,\"status_revision\":%llu,"
        "\"timestamp_ms\":%llu,\"event\":\"%s\","
        "\"status\":{\"state\":\"%s\",\"encoder_running\":%s,"
        "\"queue_drops\":%u,\"relay_failures\":%u,\"failure\":\"%s\"}}",
        (unsigned long long)relay->next_event_id,
        (unsigned long long)event->generation,
        (unsigned long long)event->status_revision,
        (unsigned long long)event->timestamp_ms, name, state,
        event->encoder_running ? "true" : "false", event->queue_drops,
        event->relay_failures, failure);
    if (written < 0 || (size_t)written >= sizeof(entry->json)) return DF_ERR_INVALID;
    entry->valid = true;
    entry->attempts = 0U;
    entry->next_attempt_ms = event->timestamp_ms;
    relay->write = (relay->write + 1U) % DF_MEDIA_RELAY_QUEUE_CAPACITY;
    relay->count++;
    relay->next_event_id++;
    if (relay->next_event_id == 0U) relay->next_event_id = 1U;
    return DF_OK;
}

static int df_media_relay_retry_or_drop(struct df_media_relay *relay,
    struct df_media_relay_entry *entry, uint64_t now_ms) {
    entry->attempts++;
    if (entry->attempts >= 3U) {
        df_media_relay_pop(relay);
        if (relay->failed < UINT_MAX) relay->failed++;
        return DF_ERR_IO;
    }
    if (now_ms > UINT64_MAX - (uint64_t)entry->attempts * 1000U)
        entry->next_attempt_ms = UINT64_MAX;
    else entry->next_attempt_ms = now_ms + (uint64_t)entry->attempts * 1000U;
    return DF_OK;
}

static int df_media_relay_curl_start(struct df_media_relay *relay,
    struct df_media_relay_entry *entry) {
    char authorization[DF_MEDIA_RELAY_TOKEN_MAX + 24U];
    struct curl_slist *headers = NULL;
    CURL *easy = curl_easy_init();

    if (easy == NULL || snprintf(authorization, sizeof(authorization),
            "Authorization: Bearer %s", relay->token) >=
            (int)sizeof(authorization)) {
        if (easy != NULL) curl_easy_cleanup(easy);
        df_media_relay_clear(authorization, sizeof(authorization));
        return DF_ERR_IO;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authorization);
    df_media_relay_clear(authorization, sizeof(authorization));
    if (headers == NULL ||
        curl_easy_setopt(easy, CURLOPT_URL, relay->url) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, entry->json) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE,
            (long)strlen(entry->json)) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 5000L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION,
            df_media_relay_discard) != CURLE_OK) {
        if (headers != NULL) curl_slist_free_all(headers);
        curl_easy_cleanup(easy);
        return DF_ERR_IO;
    }
    relay->curl_easy = easy;
    relay->curl_headers = headers;
    if (curl_multi_add_handle(relay->curl_multi, easy) != CURLM_OK) {
        df_media_relay_curl_cleanup_request(relay);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int df_media_relay_curl_tick(struct df_media_relay *relay,
    struct df_media_relay_entry *entry, uint64_t now_ms) {
    CURLMsg *message;
    int messages = 0;
    int running = 0;
    CURLMcode multi_status;

    if (relay->curl_easy == NULL &&
        df_media_relay_curl_start(relay, entry) != DF_OK)
        return df_media_relay_retry_or_drop(relay, entry, now_ms);
    multi_status = curl_multi_perform(relay->curl_multi, &running);
    if (multi_status != CURLM_OK) {
        df_media_relay_curl_cleanup_request(relay);
        return df_media_relay_retry_or_drop(relay, entry, now_ms);
    }
    while ((message = curl_multi_info_read(relay->curl_multi, &messages)) != NULL) {
        long response_code = 0L;
        bool success = message->msg == CURLMSG_DONE &&
            message->data.result == CURLE_OK &&
            curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE,
                &response_code) == CURLE_OK && response_code >= 200L &&
            response_code < 300L;
        df_media_relay_curl_cleanup_request(relay);
        if (success) {
            df_media_relay_pop(relay);
            return DF_OK;
        }
        return df_media_relay_retry_or_drop(relay, entry, now_ms);
    }
    (void)running;
    return DF_OK;
}

int df_media_relay_tick(struct df_media_relay *relay, uint64_t now_ms) {
    struct df_media_relay_entry *entry;
    int result;

    if (relay == NULL) return DF_ERR_INVALID;
    if (relay->count == 0U || relay->url[0] == '\0') return DF_OK;
    entry = &relay->entries[relay->read];
    if (!entry->valid || now_ms < entry->next_attempt_ms) return DF_OK;
    if (relay->send == NULL)
        return df_media_relay_curl_tick(relay, entry, now_ms);
    result = relay->send(relay->url, relay->token, entry->json,
                         relay->send_context);
    if (result == DF_OK) {
        df_media_relay_pop(relay);
        return DF_OK;
    }
    return df_media_relay_retry_or_drop(relay, entry, now_ms);
}

size_t df_media_relay_pending(const struct df_media_relay *relay) {
    return relay == NULL ? 0U : relay->count;
}

unsigned df_media_relay_failed(const struct df_media_relay *relay) {
    return relay == NULL ? 0U : relay->failed;
}

void df_media_relay_destroy(struct df_media_relay *relay) {
    if (relay == NULL) return;
    df_media_relay_curl_cleanup_request(relay);
    if (relay->curl_multi != NULL) curl_multi_cleanup(relay->curl_multi);
    df_media_relay_clear(relay, sizeof(*relay));
}
