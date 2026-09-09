#ifndef DOORFAST_GVS_SEND_TRANSACTION_H
#define DOORFAST_GVS_SEND_TRANSACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doorfast.h"
#include "gvs_reply_queue.h"

#define DF_GVS_SEND_MAX_ATTEMPTS 3U
#define DF_GVS_SEND_ATTEMPT_TIMEOUT_MS 250U
#define DF_GVS_SEND_RETRY_DELAY_MS 100U
#define DF_GVS_SEND_TRACE_CAPACITY 2U

enum df_gvs_send_state {
    DF_GVS_SEND_IDLE = 0,
    DF_GVS_SEND_SENDING,
    DF_GVS_SEND_WAIT_RETRY,
};

enum df_gvs_send_outcome {
    DF_GVS_SEND_OUTCOME_NONE = 0,
    DF_GVS_SEND_OUTCOME_SUCCESS,
    DF_GVS_SEND_OUTCOME_FAILED,
    DF_GVS_SEND_OUTCOME_TIMEOUT,
};

enum df_gvs_send_attempt_result {
    DF_GVS_SEND_ATTEMPT_PENDING = 0,
    DF_GVS_SEND_ATTEMPT_SUCCESS,
    DF_GVS_SEND_ATTEMPT_FAILURE,
};

enum df_gvs_send_event_type {
    DF_GVS_SEND_EVENT_SENDING = 0,
    DF_GVS_SEND_EVENT_RETRY,
    DF_GVS_SEND_EVENT_SUCCESS,
    DF_GVS_SEND_EVENT_FAILED,
    DF_GVS_SEND_EVENT_TIMEOUT,
};

struct df_gvs_send_event {
    enum df_gvs_send_event_type type;
    unsigned attempt;
    uint64_t completion_id;
    bool timed_out;
};

struct df_gvs_send_trace {
    struct df_gvs_send_event events[DF_GVS_SEND_TRACE_CAPACITY];
    size_t count;
};

struct df_gvs_send_transaction {
    enum df_gvs_send_state state;
    enum df_gvs_send_outcome last_outcome;
    struct df_gvs_reply_queue_entry entry;
    unsigned attempt;
    uint64_t active_completion_id;
    uint64_t next_completion_id;
    uint64_t deadline_ms;
    uint64_t retry_at_ms;
    uint64_t last_now_ms;
};

typedef enum df_gvs_send_attempt_result (*df_gvs_send_attempt_fn)(
    const struct df_gvs_reply_queue_entry *entry, unsigned attempt,
    uint64_t completion_id, void *context);

int df_gvs_send_transaction_init(struct df_gvs_send_transaction *transaction,
                                 uint64_t now_ms);
int df_gvs_send_transaction_step(struct df_gvs_send_transaction *transaction,
                                 struct df_gvs_reply_queue *queue,
                                 uint64_t now_ms,
                                 df_gvs_send_attempt_fn send_attempt,
                                 void *context,
                                 struct df_gvs_send_trace *trace);
int df_gvs_send_transaction_complete(
    struct df_gvs_send_transaction *transaction, uint64_t completion_id,
    enum df_gvs_send_attempt_result result, uint64_t now_ms,
    struct df_gvs_send_trace *trace);

#endif
