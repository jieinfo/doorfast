#include "gvs_reply_queue.h"

#include <limits.h>
#include <string.h>

static int df_gvs_reply_queue_prune(struct df_gvs_reply_queue *queue,
                                    uint64_t now_ms,
                                    size_t *expired_count) {
    size_t read_index;
    size_t write_index = 0;

    *expired_count = 0;
    for (read_index = 0; read_index < queue->count; read_index++) {
        if (queue->entries[read_index].expires_ms <= now_ms) {
            (*expired_count)++;
            continue;
        }
        if (write_index != read_index) {
            queue->entries[write_index] = queue->entries[read_index];
        }
        write_index++;
    }
    if (write_index < queue->count) {
        memset(queue->entries + write_index, 0,
               (queue->count - write_index) * sizeof(queue->entries[0]));
    }
    queue->count = write_index;
    queue->last_now_ms = now_ms;
    return DF_OK;
}

int df_gvs_reply_queue_init(struct df_gvs_reply_queue *queue,
                            uint64_t now_ms) {
    if (queue == NULL) {
        return DF_ERR_INVALID;
    }
    memset(queue, 0, sizeof(*queue));
    queue->last_now_ms = now_ms;
    return DF_OK;
}

int df_gvs_reply_queue_enqueue(struct df_gvs_reply_queue *queue,
                               const struct df_gvs_peer_reply *reply,
                               uint64_t now_ms, bool *coalesced) {
    struct df_gvs_reply_queue next;
    size_t expired_count;
    size_t index;

    if (coalesced != NULL) {
        *coalesced = false;
    }
    if (queue == NULL || reply == NULL || coalesced == NULL ||
        now_ms < queue->last_now_ms ||
        now_ms > UINT64_MAX - DF_GVS_REPLY_QUEUE_TTL_MS) {
        return DF_ERR_INVALID;
    }
    next = *queue;
    (void)df_gvs_reply_queue_prune(&next, now_ms, &expired_count);
    for (index = 0; index < next.count; index++) {
        if (memcmp(next.entries[index].reply.target, reply->target, 6) == 0 &&
            memcmp(next.entries[index].reply.request_data,
                   reply->request_data, 2) == 0) {
            next.entries[index].reply = *reply;
            next.entries[index].expires_ms =
                now_ms + DF_GVS_REPLY_QUEUE_TTL_MS;
            if (next.entries[index].repeat_count < UINT_MAX) {
                next.entries[index].repeat_count++;
            }
            *queue = next;
            *coalesced = true;
            return DF_OK;
        }
    }
    if (next.count >= DF_GVS_REPLY_QUEUE_CAPACITY) {
        return DF_ERR_IO;
    }
    next.entries[next.count].reply = *reply;
    next.entries[next.count].expires_ms =
        now_ms + DF_GVS_REPLY_QUEUE_TTL_MS;
    next.entries[next.count].repeat_count = 1U;
    next.count++;
    *queue = next;
    return DF_OK;
}

int df_gvs_reply_queue_expire(struct df_gvs_reply_queue *queue,
                              uint64_t now_ms, size_t *expired_count) {
    struct df_gvs_reply_queue next;
    size_t next_expired;

    if (expired_count != NULL) {
        *expired_count = 0;
    }
    if (queue == NULL || expired_count == NULL ||
        now_ms < queue->last_now_ms) {
        return DF_ERR_INVALID;
    }
    next = *queue;
    (void)df_gvs_reply_queue_prune(&next, now_ms, &next_expired);
    *queue = next;
    *expired_count = next_expired;
    return DF_OK;
}

int df_gvs_reply_queue_take(struct df_gvs_reply_queue *queue,
                            uint64_t now_ms,
                            struct df_gvs_reply_queue_entry *entry) {
    struct df_gvs_reply_queue next;
    size_t expired_count;

    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
    if (queue == NULL || entry == NULL || now_ms < queue->last_now_ms) {
        return DF_ERR_INVALID;
    }
    next = *queue;
    (void)df_gvs_reply_queue_prune(&next, now_ms, &expired_count);
    if (next.count == 0U) {
        *queue = next;
        return DF_ERR_IO;
    }
    *entry = next.entries[0];
    next.count--;
    if (next.count > 0U) {
        memmove(next.entries, next.entries + 1,
                next.count * sizeof(next.entries[0]));
    }
    memset(&next.entries[next.count], 0, sizeof(next.entries[0]));
    *queue = next;
    return DF_OK;
}

size_t df_gvs_reply_queue_count(const struct df_gvs_reply_queue *queue) {
    return queue == NULL ? 0U : queue->count;
}
