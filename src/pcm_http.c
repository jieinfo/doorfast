#include "pcm_http.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "doorfast.h"
#include "runtime_id.h"

struct pcm_state {
    char runtime_id[17];
    uint64_t generation;
    uint64_t next_sequence;
    char audio_session[33];
    uint64_t lease_deadline_ms;
};

static int respond(struct df_pcm_http_response *response, unsigned status,
                   const char *error) {
    response->http_status = status;
    (void)snprintf(response->error, sizeof(response->error), "%s", error);
    return DF_OK;
}

static bool parse_decimal(const char *text, uint64_t *value) {
    char *end;
    unsigned long long parsed;
    if (text == NULL || *text == '\0') return false;
    size_t digits = 0;
    for (const char *p = text; *p != '\0'; p++)
        if (*p < '0' || *p > '9' || ++digits > 20U) return false;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > UINT64_MAX) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool token_valid(const char *text) {
    if (text == NULL) return false;
    for (size_t i = 0; i < 32U; i++)
        if (!((text[i] >= '0' && text[i] <= '9') ||
              (text[i] >= 'a' && text[i] <= 'f'))) return false;
    return text[32] == '\0';
}

static int open_state(const char *path) {
    struct stat statbuf;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &statbuf) != 0 || !S_ISREG(statbuf.st_mode) ||
        statbuf.st_uid != geteuid() || (statbuf.st_mode & 07777) != 0600) {
        (void)close(fd);
        return -1;
    }
    while (flock(fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        (void)close(fd);
        return -1;
    }
    return fd;
}

static bool read_state(int fd, struct pcm_state *state, bool *present) {
    char record[256];
    size_t used = 0;
    unsigned seen = 0;
    memset(state, 0, sizeof(*state));
    *present = false;
    while (used < sizeof(record)) {
        ssize_t count = read(fd, record + used, sizeof(record) - used);
        if (count > 0) { used += (size_t)count; continue; }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return false;
        break;
    }
    if (used == 0U) return true;
    if (used == sizeof(record) || record[used - 1U] != '\n' ||
        memchr(record, '\0', used) != NULL) return false;
    record[used] = '\0';
    char *line = record;
    while (*line != '\0') {
        char *newline = strchr(line, '\n');
        char *equals = strchr(line, '=');
        unsigned field;
        if (newline == NULL || equals == NULL || equals > newline) return false;
        *newline = '\0';
        *equals++ = '\0';
        if (strcmp(line, "runtime_id") == 0) {
            field = 1U;
            if (!df_runtime_id_is_valid(equals)) return false;
            memcpy(state->runtime_id, equals, sizeof(state->runtime_id));
        } else if (strcmp(line, "generation") == 0) {
            field = 2U;
            if (!parse_decimal(equals, &state->generation) || state->generation == 0U)
                return false;
        } else if (strcmp(line, "next_sequence") == 0) {
            field = 4U;
            if (!parse_decimal(equals, &state->next_sequence)) return false;
        } else if (strcmp(line, "audio_session") == 0) {
            field = 8U;
            if (*equals != '\0' && !token_valid(equals)) return false;
            (void)snprintf(state->audio_session, sizeof(state->audio_session), "%s", equals);
        } else if (strcmp(line, "lease_deadline_ms") == 0) {
            field = 16U;
            if (!parse_decimal(equals, &state->lease_deadline_ms)) return false;
        } else {
            return false;
        }
        if ((seen & field) != 0U) return false;
        seen |= field;
        line = newline + 1;
    }
    if (seen != 31U || ((state->audio_session[0] == '\0') !=
                       (state->lease_deadline_ms == 0U))) return false;
    *present = true;
    return true;
}

static bool write_state(int fd, const struct pcm_state *state) {
    char record[256];
    int length = snprintf(record, sizeof(record),
        "runtime_id=%s\ngeneration=%" PRIu64 "\nnext_sequence=%" PRIu64
        "\naudio_session=%s\nlease_deadline_ms=%" PRIu64 "\n",
        state->runtime_id, state->generation, state->next_sequence,
        state->audio_session, state->lease_deadline_ms);
    if (length < 0 || (size_t)length >= sizeof(record) || lseek(fd, 0, SEEK_SET) < 0)
        return false;
    size_t offset = 0;
    while (offset < (size_t)length) {
        ssize_t count = write(fd, record + offset, (size_t)length - offset);
        if (count > 0) { offset += (size_t)count; continue; }
        if (count < 0 && errno == EINTR) continue;
        return false;
    }
    /* Keep the same inode: replacing the file would split concurrent locks. */
    return ftruncate(fd, (off_t)length) == 0;
}

static int handle_locked(const struct df_pcm_http_config *config,
                         const struct df_pcm_http_request *request,
                         struct df_pcm_http_response *response,
                         uint64_t generation, int fd) {
    struct df_pcm_http_status status = {0};
    struct pcm_state state;
    bool present;
    uint64_t now;
    if (config->ops.read_status(&status, config->ops.context) != DF_OK ||
        status.runtime_id[16] != '\0' || !df_runtime_id_is_valid(status.runtime_id) ||
        memchr(status.call_state, '\0', sizeof(status.call_state)) == NULL)
        return respond(response, 503, "status_unavailable");
    memcpy(response->runtime_id, status.runtime_id, sizeof(response->runtime_id));
    response->generation = status.generation;
    if (!read_state(fd, &state, &present))
        return respond(response, 503, "state_unavailable");
    bool matches = present && strcmp(state.runtime_id, status.runtime_id) == 0 &&
                   state.generation == status.generation;
    if (matches) response->next_sequence = state.next_sequence;
    if (strcmp(request->runtime_id, status.runtime_id) != 0)
        return respond(response, 409, "runtime_mismatch");
    if (generation != status.generation)
        return respond(response, 409, "generation_mismatch");
    if (strcmp(status.call_state, "talking") != 0)
        return respond(response, 409, "call_not_talking");
    if (!status.audio_tx_active || status.audio_tx_generation != generation)
        return respond(response, 409, "audio_tx_inactive");
    now = config->ops.now_ms(config->ops.context);
    if (request->operation == DF_PCM_HTTP_SESSION_OPEN) {
        uint8_t bytes[16];
        static const char hex[] = "0123456789abcdef";
        if (matches && state.audio_session[0] != '\0' && now < state.lease_deadline_ms)
            return respond(response, 409, "producer_busy");
        if (now > UINT64_MAX - DF_PCM_HTTP_LEASE_MS)
            return respond(response, 503, "clock_unavailable");
        if (config->ops.fill_random(bytes, sizeof(bytes), config->ops.context) != DF_OK)
            return respond(response, 503, "random_unavailable");
        if (!matches) {
            memset(&state, 0, sizeof(state));
            memcpy(state.runtime_id, status.runtime_id, sizeof(state.runtime_id));
            state.generation = generation;
        }
        for (size_t i = 0; i < sizeof(bytes); i++) {
            state.audio_session[2U * i] = hex[bytes[i] >> 4U];
            state.audio_session[2U * i + 1U] = hex[bytes[i] & 15U];
        }
        state.audio_session[32] = '\0';
        state.lease_deadline_ms = now + DF_PCM_HTTP_LEASE_MS;
    } else {
        if (!matches) return respond(response, 409, "session_mismatch");
        if (state.audio_session[0] == '\0' || now >= state.lease_deadline_ms)
            return respond(response, 409, "session_expired");
        if (strcmp(request->session_token, state.audio_session) != 0)
            return respond(response, 409, "session_mismatch");
        state.audio_session[0] = '\0';
        state.lease_deadline_ms = 0;
    }
    if (!write_state(fd, &state)) return respond(response, 503, "state_unavailable");
    response->next_sequence = state.next_sequence;
    if (request->operation == DF_PCM_HTTP_SESSION_OPEN) {
        memcpy(response->audio_session, state.audio_session, sizeof(response->audio_session));
        response->lease_ms = DF_PCM_HTTP_LEASE_MS;
    }
    return respond(response, 200, "");
}

int df_pcm_http_handle(const struct df_pcm_http_config *config,
                       const struct df_pcm_http_request *request,
                       struct df_pcm_http_response *response) {
    uint64_t generation, length;
    int fd, result;
    if (response == NULL) return DF_ERR_INVALID;
    memset(response, 0, sizeof(*response));
    if (config == NULL || request == NULL || config->state_path == NULL ||
        config->state_path[0] == '\0' || config->ops.read_status == NULL ||
        config->ops.now_ms == NULL ||
        (request->operation == DF_PCM_HTTP_SESSION_OPEN && config->ops.fill_random == NULL)) {
        (void)respond(response, 500, "invalid_config");
        return DF_ERR_INVALID;
    }
    if (request->method == NULL || strcmp(request->method, "POST") != 0)
        return respond(response, 405, "method_not_allowed");
    if (request->operation == DF_PCM_HTTP_SUBMIT)
        return respond(response, 501, "not_implemented");
    if ((request->operation != DF_PCM_HTTP_SESSION_OPEN &&
         request->operation != DF_PCM_HTTP_SESSION_END) ||
        !df_runtime_id_is_valid(request->runtime_id) ||
        !parse_decimal(request->generation, &generation) || generation == 0U ||
        !parse_decimal(request->content_length, &length) || length != 0U ||
        (request->operation == DF_PCM_HTTP_SESSION_END && !token_valid(request->session_token)))
        return respond(response, 400, "invalid_request");
    fd = open_state(config->state_path);
    if (fd < 0) return respond(response, 503, "state_unavailable");
    result = handle_locked(config, request, response, generation, fd);
    (void)close(fd);
    return result;
}
