#ifndef DOORFAST_GVS_REPLY_QUEUE_H
#define DOORFAST_GVS_REPLY_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_presence.h"

#define DF_GVS_REPLY_QUEUE_CAPACITY 16U
#define DF_GVS_REPLY_QUEUE_TTL_MS 1000U

struct df_gvs_reply_queue_entry {
    struct df_gvs_peer_reply reply;
    uint64_t expires_ms;
    unsigned repeat_count;
};

struct df_gvs_reply_queue {
    struct df_gvs_reply_queue_entry entries[DF_GVS_REPLY_QUEUE_CAPACITY];
    size_t count;
    uint64_t last_now_ms;
};

int df_gvs_reply_queue_init(struct df_gvs_reply_queue *queue,
                            uint64_t now_ms);
int df_gvs_reply_queue_enqueue(struct df_gvs_reply_queue *queue,
                               const struct df_gvs_peer_reply *reply,
                               uint64_t now_ms, bool *coalesced);
int df_gvs_reply_queue_expire(struct df_gvs_reply_queue *queue,
                              uint64_t now_ms, size_t *expired_count);
int df_gvs_reply_queue_take(struct df_gvs_reply_queue *queue,
                            uint64_t now_ms,
                            struct df_gvs_reply_queue_entry *entry);
size_t df_gvs_reply_queue_count(const struct df_gvs_reply_queue *queue);

#endif
