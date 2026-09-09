#include "gvs_send_transaction.h"

#include <limits.h>
#include <string.h>

static void df_gvs_send_trace_reset(struct df_gvs_send_trace *trace) {
    memset(trace, 0, sizeof(*trace));
}

static void df_gvs_send_trace_add(struct df_gvs_send_trace *trace,
                                  enum df_gvs_send_event_type type,
                                  unsigned attempt, uint64_t completion_id,
                                  bool timed_out) {
    struct df_gvs_send_event *event = &trace->events[trace->count++];

    event->type = type;
    event->attempt = attempt;
    event->completion_id = completion_id;
    event->timed_out = timed_out;
}

static void df_gvs_send_finish(struct df_gvs_send_transaction *transaction,
                               enum df_gvs_send_outcome outcome) {
    transaction->state = DF_GVS_SEND_IDLE;
    transaction->last_outcome = outcome;
    transaction->deadline_ms = 0;
    transaction->retry_at_ms = 0;
    transaction->active_completion_id = 0;
    memset(&transaction->entry, 0, sizeof(transaction->entry));
}

static void df_gvs_send_failed_attempt(
    struct df_gvs_send_transaction *transaction, uint64_t now_ms,
    bool timed_out, struct df_gvs_send_trace *trace) {
    if (transaction->attempt < DF_GVS_SEND_MAX_ATTEMPTS) {
        transaction->state = DF_GVS_SEND_WAIT_RETRY;
        transaction->deadline_ms = 0;
        if (now_ms > UINT64_MAX - DF_GVS_SEND_RETRY_DELAY_MS) {
            df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_TIMEOUT,
                                  transaction->attempt,
                                  transaction->active_completion_id, true);
            df_gvs_send_finish(transaction, DF_GVS_SEND_OUTCOME_TIMEOUT);
            return;
        }
        transaction->retry_at_ms = now_ms + DF_GVS_SEND_RETRY_DELAY_MS;
        df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_RETRY,
                              transaction->attempt,
                              transaction->active_completion_id, timed_out);
        return;
    }
    if (timed_out) {
        df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_TIMEOUT,
                              transaction->attempt,
                              transaction->active_completion_id, true);
        df_gvs_send_finish(transaction, DF_GVS_SEND_OUTCOME_TIMEOUT);
    } else {
        df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_FAILED,
                              transaction->attempt,
                              transaction->active_completion_id, false);
        df_gvs_send_finish(transaction, DF_GVS_SEND_OUTCOME_FAILED);
    }
}

static void df_gvs_send_apply_result(
    struct df_gvs_send_transaction *transaction,
    enum df_gvs_send_attempt_result result, uint64_t now_ms,
    struct df_gvs_send_trace *trace) {
    if (result == DF_GVS_SEND_ATTEMPT_SUCCESS) {
        df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_SUCCESS,
                              transaction->attempt,
                              transaction->active_completion_id, false);
        df_gvs_send_finish(transaction, DF_GVS_SEND_OUTCOME_SUCCESS);
    } else if (result == DF_GVS_SEND_ATTEMPT_FAILURE) {
        df_gvs_send_failed_attempt(transaction, now_ms, false, trace);
    }
}

static int df_gvs_send_start_attempt(
    struct df_gvs_send_transaction *transaction, uint64_t now_ms,
    df_gvs_send_attempt_fn send_attempt, void *context,
    struct df_gvs_send_trace *trace) {
    enum df_gvs_send_attempt_result result;

    if (transaction->next_completion_id == 0U ||
        now_ms > UINT64_MAX - DF_GVS_SEND_ATTEMPT_TIMEOUT_MS) {
        return DF_ERR_IO;
    }
    transaction->attempt++;
    transaction->active_completion_id = transaction->next_completion_id;
    transaction->next_completion_id =
        transaction->active_completion_id == UINT64_MAX
            ? 0U : transaction->active_completion_id + 1U;
    transaction->state = DF_GVS_SEND_SENDING;
    transaction->retry_at_ms = 0;
    transaction->deadline_ms = now_ms + DF_GVS_SEND_ATTEMPT_TIMEOUT_MS;
    df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_SENDING,
                          transaction->attempt,
                          transaction->active_completion_id, false);
    result = send_attempt(&transaction->entry, transaction->attempt,
                          transaction->active_completion_id, context);
    df_gvs_send_apply_result(transaction, result, now_ms, trace);
    return DF_OK;
}

int df_gvs_send_transaction_init(struct df_gvs_send_transaction *transaction,
                                 uint64_t now_ms) {
    if (transaction == NULL) {
        return DF_ERR_INVALID;
    }
    memset(transaction, 0, sizeof(*transaction));
    transaction->state = DF_GVS_SEND_IDLE;
    transaction->next_completion_id = 1U;
    transaction->last_now_ms = now_ms;
    return DF_OK;
}

int df_gvs_send_transaction_step(struct df_gvs_send_transaction *transaction,
                                 struct df_gvs_reply_queue *queue,
                                 uint64_t now_ms,
                                 df_gvs_send_attempt_fn send_attempt,
                                 void *context,
                                 struct df_gvs_send_trace *trace) {
    struct df_gvs_send_transaction next_transaction;
    struct df_gvs_reply_queue next_queue;
    struct df_gvs_reply_queue_entry entry;
    int take_status;

    if (trace != NULL) {
        df_gvs_send_trace_reset(trace);
    }
    if (transaction == NULL || queue == NULL || send_attempt == NULL ||
        trace == NULL || now_ms < transaction->last_now_ms ||
        now_ms < queue->last_now_ms ||
        (transaction->state == DF_GVS_SEND_IDLE &&
         now_ms > UINT64_MAX - DF_GVS_SEND_ATTEMPT_TIMEOUT_MS)) {
        return DF_ERR_INVALID;
    }
    next_transaction = *transaction;
    next_queue = *queue;
    next_transaction.last_now_ms = now_ms;
    if (next_transaction.state == DF_GVS_SEND_SENDING) {
        if (now_ms >= next_transaction.deadline_ms) {
            df_gvs_send_failed_attempt(&next_transaction, now_ms, true, trace);
        }
        *transaction = next_transaction;
        return DF_OK;
    }
    if (next_transaction.state == DF_GVS_SEND_WAIT_RETRY) {
        if (now_ms >= next_transaction.retry_at_ms &&
            now_ms > UINT64_MAX - DF_GVS_SEND_ATTEMPT_TIMEOUT_MS) {
            df_gvs_send_trace_add(trace, DF_GVS_SEND_EVENT_TIMEOUT,
                                  next_transaction.attempt,
                                  next_transaction.active_completion_id, true);
            df_gvs_send_finish(&next_transaction, DF_GVS_SEND_OUTCOME_TIMEOUT);
            *transaction = next_transaction;
            return DF_OK;
        }
        if (now_ms >= next_transaction.retry_at_ms &&
            df_gvs_send_start_attempt(&next_transaction, now_ms, send_attempt,
                                      context, trace) != DF_OK) {
            return DF_ERR_IO;
        }
        *transaction = next_transaction;
        return DF_OK;
    }
    take_status = df_gvs_reply_queue_take(&next_queue, now_ms, &entry);
    if (take_status == DF_ERR_IO) {
        *transaction = next_transaction;
        *queue = next_queue;
        return DF_OK;
    }
    if (take_status != DF_OK) {
        return take_status;
    }
    next_transaction.entry = entry;
    next_transaction.attempt = 0;
    next_transaction.last_outcome = DF_GVS_SEND_OUTCOME_NONE;
    if (df_gvs_send_start_attempt(&next_transaction, now_ms, send_attempt,
                                  context, trace) != DF_OK) {
        return DF_ERR_IO;
    }
    *transaction = next_transaction;
    *queue = next_queue;
    return DF_OK;
}

int df_gvs_send_transaction_complete(
    struct df_gvs_send_transaction *transaction, uint64_t completion_id,
    enum df_gvs_send_attempt_result result, uint64_t now_ms,
    struct df_gvs_send_trace *trace) {
    struct df_gvs_send_transaction next;

    if (trace != NULL) {
        df_gvs_send_trace_reset(trace);
    }
    if (transaction == NULL || trace == NULL ||
        transaction->state != DF_GVS_SEND_SENDING ||
        completion_id != transaction->active_completion_id ||
        now_ms < transaction->last_now_ms ||
        (result != DF_GVS_SEND_ATTEMPT_SUCCESS &&
         result != DF_GVS_SEND_ATTEMPT_FAILURE)) {
        return DF_ERR_INVALID;
    }
    next = *transaction;
    next.last_now_ms = now_ms;
    if (now_ms >= next.deadline_ms) {
        df_gvs_send_failed_attempt(&next, now_ms, true, trace);
    } else {
        df_gvs_send_apply_result(&next, result, now_ms, trace);
    }
    *transaction = next;
    return DF_OK;
}
