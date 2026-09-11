#include "gvs_call_dispatch.h"
#include "gvs_memory_sender.h"
#include <string.h>

enum df_gvs_send_attempt_result df_gvs_call_memory_attempt(
    const struct df_gvs_call_command *command, unsigned attempt,
    uint64_t id, void *context) {
    struct df_gvs_call_memory_sender *sender = context;
    uint8_t bytes[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
    size_t length;
    if (sender == NULL || attempt == 0 || id == 0 ||
        df_gvs_call_command_serialize(command, bytes, sizeof(bytes), &length,
            sender->provide_fields, sender->fields_context) != DF_OK)
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    memcpy(sender->bytes, bytes, length);
    sender->length = length;
    sender->completion_id = id;
    return DF_GVS_SEND_ATTEMPT_SUCCESS;
}

static bool active(const struct df_gvs_call_dispatch *d) {
    return d->state == DF_GVS_CALL_QUEUED || d->state == DF_GVS_CALL_SENDING ||
           d->state == DF_GVS_CALL_RETRY;
}

static bool current(const struct df_gvs_call_dispatch *d,
    const struct df_gvs_session *s, const uint8_t local[6]) {
    return d->command.session_generation == s->generation &&
        memcmp(d->command.destination, s->peer, 6) == 0 &&
        memcmp(d->command.source, local, 6) == 0 &&
        (d->command.type == DF_GVS_CALL_COMMAND_ANSWER
            ? s->state == DF_GVS_RINGING
            : (s->state == DF_GVS_PREVIEW || s->state == DF_GVS_RINGING ||
               s->state == DF_GVS_TALKING));
}

static void finish(struct df_gvs_call_dispatch *d,
    enum df_gvs_call_dispatch_state state) {
    d->state = state;
    d->active_id = 0;
    d->deadline_ms = 0;
}

static void failed(struct df_gvs_call_dispatch *d, uint64_t now, bool timeout) {
    d->active_id = 0;
    if (d->attempts >= DF_GVS_SEND_MAX_ATTEMPTS) {
        finish(d, timeout ? DF_GVS_CALL_TIMEOUT : DF_GVS_CALL_FAILED);
    } else if (now > UINT64_MAX - DF_GVS_SEND_RETRY_DELAY_MS) {
        finish(d, DF_GVS_CALL_TIMEOUT);
    } else {
        d->state = DF_GVS_CALL_RETRY;
        d->deadline_ms = now + DF_GVS_SEND_RETRY_DELAY_MS;
    }
}

void df_gvs_call_dispatch_init(struct df_gvs_call_dispatch *d, uint64_t now) {
    if (d == NULL) return;
    memset(d, 0, sizeof(*d));
    d->last_now_ms = now;
    d->next_id = 1;
}

int df_gvs_call_dispatch_enqueue(struct df_gvs_call_dispatch *d,
    const struct df_gvs_call_command *c, uint64_t now) {
    struct df_gvs_call_command checked;
    struct df_gvs_session s = {0};
    int status;
    if (d == NULL || c == NULL || now < d->last_now_ms || !c->valid)
        return DF_ERR_INVALID;
    if (active(d)) return DF_ERR_IO;
    s.state = DF_GVS_RINGING;
    s.generation = c->session_generation;
    memcpy(s.peer, c->destination, 6);
    if (c->type == DF_GVS_CALL_COMMAND_ANSWER && c->payload_length == 7) {
        status = df_gvs_call_command_prepare_answer(&s, s.generation, c->source,
            (uint16_t)((c->payload[1] << 8) | c->payload[2]),
            (uint16_t)((c->payload[4] << 8) | c->payload[5]), c->payload[6], &checked);
    } else if (c->type == DF_GVS_CALL_COMMAND_HANGUP && c->payload_length == 1) {
        status = df_gvs_call_command_prepare_hangup(&s, s.generation,
            c->source, c->payload[0], &checked);
    } else if (c->type == DF_GVS_CALL_COMMAND_HAND_ASK ||
               c->type == DF_GVS_CALL_COMMAND_HAND_REPLY) {
        uint8_t bytes[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
        size_t length;
        status = df_gvs_call_command_serialize(c, bytes, sizeof(bytes),
            &length, df_gvs_placeholder_header_fields, NULL);
        checked = *c;
    } else return DF_ERR_INVALID;
    if (status != DF_OK || checked.opcode != c->opcode ||
        memcmp(checked.payload, c->payload, c->payload_length) != 0)
        return DF_ERR_INVALID;
    if (d->next_id == 0 || now > UINT64_MAX - DF_GVS_SEND_ATTEMPT_TIMEOUT_MS)
        return DF_ERR_IO;
    d->command = checked;
    d->state = DF_GVS_CALL_QUEUED;
    d->last_now_ms = now;
    d->deadline_ms = now + DF_GVS_SEND_ATTEMPT_TIMEOUT_MS;
    d->attempts = 0;
    d->active_id = 0;
    return DF_OK;
}

int df_gvs_call_dispatch_step(struct df_gvs_call_dispatch *d,
    const struct df_gvs_session *s, const uint8_t local[6], uint64_t now,
    df_gvs_call_attempt_fn send, void *context) {
    enum df_gvs_send_attempt_result result;
    if (d == NULL || s == NULL || local == NULL || send == NULL ||
        now < d->last_now_ms) return DF_ERR_INVALID;
    d->last_now_ms = now;
    if (!active(d)) return DF_OK;
    if (!current(d,s,local)) {
        finish(d,DF_GVS_CALL_CANCELLED);
        return DF_OK;
    }
    if (d->state == DF_GVS_CALL_SENDING) {
        if (now >= d->deadline_ms) failed(d,now,true);
        return DF_OK;
    }
    if (d->state == DF_GVS_CALL_RETRY && now < d->deadline_ms) return DF_OK;
    if ((d->state == DF_GVS_CALL_QUEUED && now >= d->deadline_ms) ||
        now > UINT64_MAX - DF_GVS_SEND_ATTEMPT_TIMEOUT_MS) {
        finish(d,DF_GVS_CALL_TIMEOUT);
        return DF_OK;
    }
    if (d->next_id == 0) {
        finish(d,DF_GVS_CALL_FAILED);
        return DF_OK;
    }
    d->active_id = d->next_id++;
    d->attempts++;
    d->state = DF_GVS_CALL_SENDING;
    d->deadline_ms = now + DF_GVS_SEND_ATTEMPT_TIMEOUT_MS;
    result = send(&d->command,d->attempts,d->active_id,context);
    if (result == DF_GVS_SEND_ATTEMPT_SUCCESS) finish(d,DF_GVS_CALL_SENT);
    else if (result != DF_GVS_SEND_ATTEMPT_PENDING) failed(d,now,false);
    return DF_OK;
}

int df_gvs_call_dispatch_complete(struct df_gvs_call_dispatch *d,
    const struct df_gvs_session *s, const uint8_t local[6], uint64_t now,
    uint64_t id, enum df_gvs_send_attempt_result result) {
    if (d == NULL || s == NULL || local == NULL || now < d->last_now_ms ||
        d->state != DF_GVS_CALL_SENDING || id == 0 || id != d->active_id ||
        (result != DF_GVS_SEND_ATTEMPT_SUCCESS && result != DF_GVS_SEND_ATTEMPT_FAILURE))
        return DF_ERR_INVALID;
    d->last_now_ms = now;
    if (!current(d,s,local)) finish(d,DF_GVS_CALL_CANCELLED);
    else if (now >= d->deadline_ms) failed(d,now,true);
    else if (result == DF_GVS_SEND_ATTEMPT_SUCCESS) finish(d,DF_GVS_CALL_SENT);
    else failed(d,now,false);
    return DF_OK;
}
