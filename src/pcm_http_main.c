#include "pcm_http.h"

#if defined(DF_PCM_HTTP_PROGRAM) || defined(DF_PCM_HTTP_TEST_PROGRAM) || defined(DF_PCM_HTTP_ACCEPTANCE)

#if (defined(DF_PCM_HTTP_PROGRAM) + defined(DF_PCM_HTTP_TEST_PROGRAM) + defined(DF_PCM_HTTP_ACCEPTANCE)) != 1
#error "PCM HTTP program modes are mutually exclusive"
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef DF_PCM_HTTP_PROGRAM
#include "pcm_http_ubus.h"
#endif
#ifdef DF_PCM_HTTP_ACCEPTANCE
#include <sys/stat.h>
#include "gvs_pcm_ingress.h"
#include "runtime_id.h"
#endif

#define DF_PCM_HTTP_STATE_PATH "/tmp/doorfast-pcm-http.state"
#define DF_PCM_HTTP_SOCKET_PATH "/var/run/doorfast-audio.sock"
#define DF_PCM_HTTP_QUERY_MAX 255U

struct query_fields {
    char storage[DF_PCM_HTTP_QUERY_MAX + 1U];
    const char *runtime_id;
    const char *generation;
    const char *sequence;
};

static uint64_t monotonic_now_ms(void *context)
{
    struct timespec timestamp;

    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return 0;
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000) +
           (uint64_t)timestamp.tv_nsec / UINT64_C(1000000);
}

static int monotonic_sleep_until_ms(uint64_t deadline_ms, void *context)
{
    struct timespec deadline;

    (void)context;
    deadline.tv_sec = (time_t)(deadline_ms / UINT64_C(1000));
    deadline.tv_nsec = (long)((deadline_ms % UINT64_C(1000)) *
                              UINT64_C(1000000));
#ifdef __linux__
    for (;;) {
        int result = clock_nanosleep(
            CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);

        if (result == 0)
            return 0;
        if (result != EINTR)
            return -1;
    }
#else
    /* macOS has CLOCK_MONOTONIC but no clock_nanosleep. Recompute the
     * remaining interval after each interruption to retain an absolute
     * monotonic deadline for host tests. */
    for (;;) {
        struct timespec current;
        struct timespec remaining;

        if (clock_gettime(CLOCK_MONOTONIC, &current) != 0)
            return -1;
        remaining.tv_sec = deadline.tv_sec - current.tv_sec;
        remaining.tv_nsec = deadline.tv_nsec - current.tv_nsec;
        if (remaining.tv_nsec < 0) {
            remaining.tv_sec--;
            remaining.tv_nsec += 1000000000L;
        }
        if (remaining.tv_sec < 0)
            return 0;
        if (nanosleep(&remaining, NULL) == 0)
            return 0;
        if (errno != EINTR)
            return -1;
    }
#endif
}

#ifdef DF_PCM_HTTP_TEST_PROGRAM

static int test_status(struct df_pcm_http_status *status, void *context)
{
    (void)context;
    memset(status, 0, sizeof(*status));
    memcpy(status->runtime_id, "0123456789abcdef", 17U);
    memcpy(status->call_state, "talking", 8U);
    status->generation = 7U;
    status->audio_tx_active = true;
    status->audio_tx_generation = 7U;
    return 0;
}

static int test_random(uint8_t *bytes, size_t length, void *context)
{
    (void)context;
    memset(bytes, 0xab, length);
    return 0;
}

static int write_all(int fd, const uint8_t *bytes, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = write(fd, bytes + offset, length - offset);

        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static int test_send_pcm(const char *path, uint64_t generation,
                         const int16_t *pcm, size_t sample_count,
                         void *context)
{
    uint8_t bytes[DF_PCM_HTTP_FRAME_BYTES];
    const char *sink = getenv("DF_PCM_HTTP_TEST_SINK");
    int fd;

    (void)path;
    (void)context;
    if (generation != 7U || pcm == NULL || sample_count != 160U ||
        sink == NULL || sink[0] == '\0')
        return -1;
    for (size_t index = 0; index < sample_count; index++) {
        uint16_t sample = (uint16_t)pcm[index];

        bytes[index * 2U] = (uint8_t)(sample & 0xffU);
        bytes[index * 2U + 1U] = (uint8_t)(sample >> 8U);
    }
    fd = open(sink, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (write_all(fd, bytes, sizeof(bytes)) != 0) {
        (void)close(fd);
        return -1;
    }
    return close(fd) == 0 ? 0 : -1;
}

#else

#ifdef DF_PCM_HTTP_PROGRAM
static int production_status(struct df_pcm_http_status *status, void *context)
{
    return df_pcm_http_ubus_status(status, context);
}
#endif

static int production_random(uint8_t *bytes, size_t length, void *context)
{
    size_t offset = 0;
    int fd;

    (void)context;
    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    while (offset < length) {
        ssize_t count = read(fd, bytes + offset, length - offset);

        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        (void)close(fd);
        return -1;
    }
    return close(fd) == 0 ? 0 : -1;
}

#endif

#ifdef DF_PCM_HTTP_ACCEPTANCE

static bool acceptance_decimal(const char *text, uint64_t *value)
{
    uint64_t parsed = 0;
    size_t digits = 0;

    if (*text == '\0')
        return false;
    for (; *text != '\0'; text++) {
        unsigned digit = (unsigned)(*text - '0');
        if (digit > 9U || ++digits > 20U ||
            parsed > (UINT64_MAX - digit) / 10U)
            return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

static int acceptance_status(struct df_pcm_http_status *status, void *context)
{
    const char *path = context;
    struct stat info;
    struct df_pcm_http_status parsed = {0};
    char record[256];
    size_t used = 0;
    unsigned seen = 0;
    /* NONBLOCK prevents a FIFO fixture from hanging before the type check. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0)
        return -1;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 07777) != 0600) {
        (void)close(fd);
        return -1;
    }
    while (used < sizeof(record)) {
        ssize_t count = read(fd, record + used, sizeof(record) - used);
        if (count > 0) {
            used += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            (void)close(fd);
            return -1;
        }
        break;
    }
    if (close(fd) != 0 || used == 0U || used == sizeof(record) ||
        record[used - 1U] != '\n' || memchr(record, '\0', used) != NULL)
        return -1;
    record[used] = '\0';
    char *line = record;
    while (*line != '\0') {
        char *newline = strchr(line, '\n');
        char *equals = strchr(line, '=');
        unsigned field;
        if (newline == NULL || equals == NULL || equals > newline)
            return -1;
        *newline = '\0';
        *equals++ = '\0';
        if (strcmp(line, "runtime_id") == 0) {
            field = 1U;
            if (!df_runtime_id_is_valid(equals))
                return -1;
            memcpy(parsed.runtime_id, equals, sizeof(parsed.runtime_id));
        } else if (strcmp(line, "call_state") == 0) {
            field = 2U;
            if (strcmp(equals, "idle") != 0 && strcmp(equals, "preview") != 0 &&
                strcmp(equals, "ringing") != 0 && strcmp(equals, "talking") != 0 &&
                strcmp(equals, "ended") != 0)
                return -1;
            (void)snprintf(parsed.call_state, sizeof(parsed.call_state), "%s", equals);
        } else if (strcmp(line, "generation") == 0) {
            field = 4U;
            if (!acceptance_decimal(equals, &parsed.generation))
                return -1;
        } else if (strcmp(line, "audio_tx_active") == 0) {
            field = 8U;
            if (strcmp(equals, "0") != 0 && strcmp(equals, "1") != 0)
                return -1;
            parsed.audio_tx_active = *equals == '1';
        } else if (strcmp(line, "audio_tx_generation") == 0) {
            field = 16U;
            if (!acceptance_decimal(equals, &parsed.audio_tx_generation))
                return -1;
        } else {
            return -1;
        }
        if ((seen & field) != 0U)
            return -1;
        seen |= field;
        line = newline + 1;
    }
    if (seen != 31U)
        return -1;
    *status = parsed;
    return 0;
}

static int acceptance_send(const char *path, uint64_t generation,
                           const int16_t *pcm, size_t sample_count, void *context)
{
    (void)context;
    return df_gvs_pcm_ingress_send(path, generation, pcm, sample_count);
}

static bool acceptance_arguments(int argc, char **argv, const char **status_path,
                                 const char **state_path, const char **socket_path)
{
    unsigned seen = 0;

    if (argc != 7)
        return false;
    for (int index = 1; index < argc; index += 2) {
        unsigned field;
        const char **destination;
        if (strcmp(argv[index], "--status-file") == 0) {
            field = 1U;
            destination = status_path;
        } else if (strcmp(argv[index], "--state") == 0) {
            field = 2U;
            destination = state_path;
        } else if (strcmp(argv[index], "--socket") == 0) {
            field = 4U;
            destination = socket_path;
        } else {
            return false;
        }
        if ((seen & field) != 0U || argv[index + 1][0] == '\0')
            return false;
        seen |= field;
        *destination = argv[index + 1];
    }
    return seen == 7U;
}

#endif

static bool operation_for_path(const char *path,
                               enum df_pcm_http_operation *operation)
{
    if (path == NULL || operation == NULL)
        return false;
    if (strcmp(path, "/api/v1/audio/session") == 0)
        *operation = DF_PCM_HTTP_SESSION_OPEN;
    else if (strcmp(path, "/api/v1/audio/submit.pcm") == 0)
        *operation = DF_PCM_HTTP_SUBMIT;
    else if (strcmp(path, "/api/v1/audio/session/end") == 0)
        *operation = DF_PCM_HTTP_SESSION_END;
    else
        return false;
    return true;
}

static bool parse_query(const char *query, enum df_pcm_http_operation operation,
                        struct query_fields *fields)
{
    size_t length;
    char *item;

    if (query == NULL || fields == NULL)
        return false;
    length = strlen(query);
    if (length == 0U || length > DF_PCM_HTTP_QUERY_MAX)
        return false;
    memset(fields, 0, sizeof(*fields));
    memcpy(fields->storage, query, length + 1U);
    item = fields->storage;
    while (*item != '\0') {
        char *separator = strchr(item, '&');
        char *equals;
        const char **destination;

        if (separator != NULL)
            *separator = '\0';
        if (*item == '\0')
            return false;
        equals = strchr(item, '=');
        if (equals == NULL || equals == item || equals[1] == '\0' ||
            strchr(equals + 1, '=') != NULL)
            return false;
        *equals++ = '\0';
        if (strcmp(item, "runtime") == 0)
            destination = &fields->runtime_id;
        else if (strcmp(item, "generation") == 0)
            destination = &fields->generation;
        else if (strcmp(item, "sequence") == 0 &&
                 operation == DF_PCM_HTTP_SUBMIT)
            destination = &fields->sequence;
        else
            return false;
        if (*destination != NULL)
            return false;
        *destination = equals;
        if (separator == NULL)
            break;
        item = separator + 1;
        if (*item == '\0')
            return false;
    }
    return fields->runtime_id != NULL && fields->generation != NULL &&
           (operation != DF_PCM_HTTP_SUBMIT || fields->sequence != NULL);
}

static const char *status_reason(unsigned status)
{
    switch (status) {
    case 200U: return "OK";
    case 400U: return "Bad Request";
    case 404U: return "Not Found";
    case 405U: return "Method Not Allowed";
    case 409U: return "Conflict";
    case 500U: return "Internal Server Error";
    case 503U: return "Service Unavailable";
    default: return "Internal Server Error";
    }
}

static void render_response(const struct df_pcm_http_response *response)
{
    char json[512];
    int length;

    (void)printf("Status: %u %s\r\nContent-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n\r\n",
                 response->http_status, status_reason(response->http_status));
    if (response->http_status == 200U) {
        length = snprintf(json, sizeof(json),
            "{\"status\":\"success\",\"runtime_id\":\"%s\","
            "\"generation\":%" PRIu64 ",\"audio_session\":\"%s\","
            "\"accepted_frames\":%" PRIu64 ",\"next_sequence\":%" PRIu64
            ",\"lease_ms\":%" PRIu64 "}\n",
            response->runtime_id, response->generation,
            response->audio_session, response->accepted_frames,
            response->next_sequence, response->lease_ms);
    } else if (response->runtime_id[0] != '\0') {
        length = snprintf(json, sizeof(json),
            "{\"error\":\"%s\",\"runtime_id\":\"%s\","
            "\"generation\":%" PRIu64 ",\"accepted_frames\":%" PRIu64
            ",\"next_sequence\":%" PRIu64 "}\n",
            response->error, response->runtime_id, response->generation,
            response->accepted_frames, response->next_sequence);
    } else {
        length = snprintf(json, sizeof(json),
                          "{\"error\":\"%s\"}\n", response->error);
    }
    if (length > 0 && (size_t)length < sizeof(json))
        (void)fwrite(json, 1U, (size_t)length, stdout);
}

static void render_simple_error(unsigned status, const char *error)
{
    struct df_pcm_http_response response = {0};

    response.http_status = status;
    (void)snprintf(response.error, sizeof(response.error), "%s", error);
    render_response(&response);
}

#ifdef DF_PCM_HTTP_ACCEPTANCE
int main(int argc, char **argv)
#else
int main(void)
#endif
{
    enum df_pcm_http_operation operation;
    struct query_fields fields;
    struct df_pcm_http_request request;
    struct df_pcm_http_config config;
    struct df_pcm_http_response response;
    const char *path = getenv("PATH_INFO");
    const char *state_path = DF_PCM_HTTP_STATE_PATH;
    const char *socket_path = DF_PCM_HTTP_SOCKET_PATH;

#ifdef DF_PCM_HTTP_ACCEPTANCE
    const char *status_path = NULL;
    if (!acceptance_arguments(argc, argv, &status_path, &state_path, &socket_path)) {
        render_simple_error(500U, "invalid_config");
        return 0;
    }
#endif
    if (!operation_for_path(path, &operation)) {
        render_simple_error(404U, "not_found");
        return 0;
    }
    if (!parse_query(getenv("QUERY_STRING"), operation, &fields)) {
        render_simple_error(400U, "invalid_request");
        return 0;
    }
#ifdef DF_PCM_HTTP_TEST_PROGRAM
    state_path = getenv("DF_PCM_HTTP_TEST_STATE");
    socket_path = "test-only";
    if (state_path == NULL || state_path[0] == '\0') {
        render_simple_error(500U, "invalid_config");
        return 0;
    }
#endif
    memset(&request, 0, sizeof(request));
    request.operation = operation;
    request.method = getenv("REQUEST_METHOD");
    request.runtime_id = fields.runtime_id;
    request.generation = fields.generation;
    request.sequence = fields.sequence;
    request.session_token = getenv("HTTP_X_DOORFAST_AUDIO_SESSION");
    request.content_type = getenv("CONTENT_TYPE");
    request.content_length = getenv("CONTENT_LENGTH");
    request.body_fd = STDIN_FILENO;

    memset(&config, 0, sizeof(config));
    config.state_path = state_path;
    config.socket_path = socket_path;
    config.ops.now_ms = monotonic_now_ms;
    config.ops.sleep_until_ms = monotonic_sleep_until_ms;
#ifdef DF_PCM_HTTP_TEST_PROGRAM
    config.ops.read_status = test_status;
    config.ops.fill_random = test_random;
    config.ops.send_pcm = test_send_pcm;
#elif defined(DF_PCM_HTTP_ACCEPTANCE)
    config.ops.read_status = acceptance_status;
    config.ops.fill_random = production_random;
    config.ops.send_pcm = acceptance_send;
    config.ops.context = (void *)status_path;
#else
    config.ops.read_status = production_status;
    config.ops.fill_random = production_random;
    config.ops.send_pcm = df_pcm_http_ubus_send;
#endif
    (void)df_pcm_http_handle(&config, &request, &response);
    render_response(&response);
    return 0;
}

#else

typedef int df_pcm_http_program_disabled;

#endif
