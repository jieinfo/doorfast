#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "doorfast.h"
#include "pcm_http.h"
#include "test.h"

#define RUNTIME "0123456789abcdef"
#define NEW_RUNTIME "fedcba9876543210"
#define TOKEN "000102030405060708090a0b0c0d0e0f"
#define RECORD "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\n" \
    "audio_session=" TOKEN "\nlease_deadline_ms=3000\n"

struct pcm_fake {
    struct df_pcm_http_status status;
    uint64_t now;
    unsigned random_seed;
    int status_result;
    int random_result;
    unsigned sends;
    unsigned sleeps;
    unsigned fail_send;
    unsigned fail_sleep;
    uint64_t send_duration;
    uint64_t send_times[10];
    uint64_t deadlines[10];
    int16_t samples[10][160];
    const char *state_path;
    uint64_t persisted_sequence[10];
    uint64_t persisted_lease[10];
};

struct pcm_fixture {
    char directory[64];
    char path[96];
    struct pcm_fake fake;
    struct df_pcm_http_config config;
    struct df_pcm_http_request request;
};

static int pcm_read_status(struct df_pcm_http_status *status, void *context) {
    struct pcm_fake *fake = context;
    *status = fake->status;
    return fake->status_result;
}

static int pcm_fill_random(uint8_t *bytes, size_t length, void *context) {
    struct pcm_fake *fake = context;
    for (size_t i = 0; i < length; i++)
        bytes[i] = (uint8_t)(i + fake->random_seed);
    fake->random_seed++;
    return fake->random_result;
}

static uint64_t pcm_now(void *context) {
    return ((struct pcm_fake *)context)->now;
}

static int pcm_sleep(uint64_t deadline, void *context) {
    struct pcm_fake *fake = context;
    TEST_ASSERT_INT_EQ(1, fake->sleeps < 10);
    if (fake->sleeps >= 10) return DF_ERR_IO;
    fake->deadlines[fake->sleeps++] = deadline;
    if (fake->sleeps == fake->fail_sleep) return DF_ERR_IO;
    if (fake->now < deadline) fake->now = deadline;
    return DF_OK;
}

static int pcm_send(const char *path, uint64_t generation, const int16_t *pcm,
                    size_t count, void *context) {
    struct pcm_fake *fake = context;
    TEST_ASSERT_INT_EQ(0, strcmp("/tmp/test-pcm.sock", path));
    TEST_ASSERT_INT_EQ(1, generation == 42);
    TEST_ASSERT_INT_EQ(160, (int)count);
    TEST_ASSERT_INT_EQ(1, fake->sends < 10);
    if (fake->sends >= 10 || count != 160) return DF_ERR_IO;
    unsigned index = fake->sends++;
    fake->send_times[index] = fake->now;
    memcpy(fake->samples[index], pcm, sizeof(fake->samples[index]));
    /* Observe the actual persisted prefix at each send boundary. */
    FILE *state = fopen(fake->state_path, "r");
    TEST_ASSERT_INT_EQ(1, state != NULL);
    if (state != NULL) {
        char line[128];
        while (fgets(line, sizeof(line), state) != NULL) {
            (void)sscanf(line, "next_sequence=%" SCNu64, &fake->persisted_sequence[index]);
            (void)sscanf(line, "lease_deadline_ms=%" SCNu64, &fake->persisted_lease[index]);
        }
        fclose(state);
    }
    fake->now += fake->send_duration;
    return fake->sends == fake->fail_send ? DF_ERR_IO : DF_OK;
}

static void pcm_fixture_init(struct pcm_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    strcpy(fixture->directory, "/tmp/doorfast-http-XXXXXX");
    TEST_ASSERT_INT_EQ(1, mkdtemp(fixture->directory) != NULL);
    (void)snprintf(fixture->path, sizeof(fixture->path), "%s/state", fixture->directory);
    strcpy(fixture->fake.status.runtime_id, RUNTIME);
    strcpy(fixture->fake.status.call_state, "talking");
    fixture->fake.status.generation = 42;
    fixture->fake.status.audio_tx_active = true;
    fixture->fake.status.audio_tx_generation = 42;
    fixture->fake.now = 1000;
    fixture->config.state_path = fixture->path;
    fixture->fake.state_path = fixture->path;
    fixture->config.socket_path = "/tmp/test-pcm.sock";
    fixture->config.ops.read_status = pcm_read_status;
    fixture->config.ops.fill_random = pcm_fill_random;
    fixture->config.ops.now_ms = pcm_now;
    fixture->config.ops.sleep_until_ms = pcm_sleep;
    fixture->config.ops.send_pcm = pcm_send;
    fixture->config.ops.context = &fixture->fake;
    fixture->request.operation = DF_PCM_HTTP_SESSION_OPEN;
    fixture->request.method = "POST";
    fixture->request.runtime_id = RUNTIME;
    fixture->request.generation = "42";
    fixture->request.content_length = "0";
    fixture->request.body_fd = -1;
}

static void pcm_fixture_clear(struct pcm_fixture *fixture) {
    if (fixture->request.body_fd >= 0) close(fixture->request.body_fd);
    (void)unlink(fixture->path);
    (void)rmdir(fixture->path);
    TEST_ASSERT_INT_EQ(0, rmdir(fixture->directory));
}

static struct df_pcm_http_response pcm_handle(struct pcm_fixture *fixture,
                                              unsigned expected) {
    struct df_pcm_http_response response;
    memset(&response, 0xa5, sizeof(response));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_pcm_http_handle(&fixture->config, &fixture->request, &response));
    TEST_ASSERT_INT_EQ((int)expected, (int)response.http_status);
    return response;
}

static void pcm_write_record(struct pcm_fixture *fixture, const char *record) {
    int fd = open(fixture->path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    TEST_ASSERT_INT_EQ((int)strlen(record), (int)write(fd, record, strlen(record)));
    TEST_ASSERT_INT_EQ(0, close(fd));
}

static void pcm_assert_record(struct pcm_fixture *fixture, const char *expected) {
    char buffer[1024] = {0};
    int fd = open(fixture->path, O_RDONLY);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    ssize_t count = read(fd, buffer, sizeof(buffer) - 1U);
    TEST_ASSERT_INT_EQ((int)strlen(expected), (int)count);
    TEST_ASSERT_INT_EQ(0, strcmp(expected, buffer));
    TEST_ASSERT_INT_EQ(0, close(fd));
}

static void pcm_submit_init(struct pcm_fixture *fixture) {
    pcm_fixture_init(fixture);
    pcm_write_record(fixture, RECORD);
    fixture->request.operation = DF_PCM_HTTP_SUBMIT;
    fixture->request.content_type = "application/octet-stream";
    fixture->request.content_length = "320";
    fixture->request.sequence = "100";
    fixture->request.session_token = TOKEN;
}

static void pcm_body(struct pcm_fixture *fixture, const uint8_t *data, size_t size) {
    int fds[2];
    if (fixture->request.body_fd >= 0) close(fixture->request.body_fd);
    TEST_ASSERT_INT_EQ(0, pipe(fds));
    if (size != 0) TEST_ASSERT_INT_EQ((int)size, (int)write(fds[1], data, size));
    close(fds[1]);
    fixture->request.body_fd = fds[0];
}

static void pcm_assert_rejected(struct pcm_fixture *fixture, unsigned http_status) {
    struct df_pcm_http_response response = pcm_handle(fixture, http_status);
    TEST_ASSERT_INT_EQ(0, (int)response.accepted_frames);
    TEST_ASSERT_INT_EQ(0, (int)fixture->fake.sends);
    TEST_ASSERT_INT_EQ(0, (int)fixture->fake.sleeps);
    pcm_assert_record(fixture, RECORD);
}

static void pcm_test_submit_validation(void) {
    struct pcm_fixture fixture;
    static const char *const bad_types[] = {NULL, "", "audio/pcm", "text/plain", "Application/octet-stream", "application/octet-stream; charset=utf-8"};
    static const char *const bad_lengths[] = {NULL, "", "0", "1", "319", "321", "639", "641", "959", "961", "1279", "1281", "1599", "1601", "1920", "-320", "+320", "320 ", "320x", "18446744073709551616"};
    static const char *const bad_sequences[] = {NULL, "", "-1", "+100", " 100", "100 ", "100x", "18446744073709551616", "000000000000000000100"};
    static const char *const bad_tokens[] = {NULL, "", "bad", "000102030405060708090a0b0c0d0e0F"};
    pcm_submit_init(&fixture);
    for (size_t i = 0; i < sizeof(bad_types) / sizeof(*bad_types); i++) {
        fixture.request.content_type = bad_types[i];
        pcm_assert_rejected(&fixture, 400);
    }
    fixture.request.content_type = "application/octet-stream";
    for (size_t i = 0; i < sizeof(bad_lengths) / sizeof(*bad_lengths); i++) {
        fixture.request.content_length = bad_lengths[i];
        pcm_assert_rejected(&fixture, 400);
    }
    fixture.request.content_length = "320";
    fixture.request.generation = "0";
    pcm_assert_rejected(&fixture, 400);
    fixture.request.generation = "42";
    for (size_t i = 0; i < sizeof(bad_sequences) / sizeof(*bad_sequences); i++) {
        fixture.request.sequence = bad_sequences[i];
        pcm_assert_rejected(&fixture, 400);
    }
    fixture.request.sequence = "18446744073709551615";
    pcm_assert_rejected(&fixture, 400);
    fixture.request.sequence = "18446744073709551611";
    fixture.request.content_length = "1600";
    pcm_assert_rejected(&fixture, 400);
    fixture.request.sequence = "100";
    fixture.request.content_length = "320";
    for (size_t i = 0; i < sizeof(bad_tokens) / sizeof(*bad_tokens); i++) {
        fixture.request.session_token = bad_tokens[i];
        pcm_assert_rejected(&fixture, 400);
    }
    pcm_fixture_clear(&fixture);
}

static void pcm_test_body_admission(void) {
    struct pcm_fixture fixture;
    uint8_t body[1601] = {0};
    static const size_t sizes[] = {0, 319, 321, 1601, 1599};
    pcm_submit_init(&fixture);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++) {
        fixture.request.content_length = i < 3 ? "320" : "1600";
        pcm_body(&fixture, body, sizes[i]);
        pcm_assert_rejected(&fixture, 400);
    }
    close(fixture.request.body_fd);
    fixture.request.body_fd = -1;
    pcm_assert_rejected(&fixture, 400);
    pcm_fixture_clear(&fixture);
}

static void pcm_test_submit_admission(void) {
    struct pcm_fixture fixture;
    uint8_t body[320] = {0};
    struct df_pcm_http_response response;
    pcm_submit_init(&fixture);
    for (unsigned scenario = 0; scenario < 10; scenario++) {
        fixture.request.session_token = TOKEN;
        fixture.fake.now = 1000;
        strcpy(fixture.fake.status.runtime_id, RUNTIME);
        fixture.fake.status.generation = 42;
        strcpy(fixture.fake.status.call_state, "talking");
        fixture.fake.status.audio_tx_active = true;
        fixture.fake.status.audio_tx_generation = 42;
        fixture.request.sequence = "100";
        switch (scenario) {
        case 0: fixture.request.session_token = "ffffffffffffffffffffffffffffffff"; break;
        case 1: fixture.fake.now = 3000; break;
        case 2: strcpy(fixture.fake.status.runtime_id, NEW_RUNTIME); break;
        case 3: fixture.fake.status.generation = 43; break;
        case 4: strcpy(fixture.fake.status.call_state, "ringing"); break;
        case 5: fixture.fake.status.audio_tx_active = false; break;
        case 6: fixture.fake.status.audio_tx_generation = 41; break;
        case 7: fixture.request.sequence = "99"; break;
        case 8: fixture.request.sequence = "101"; break;
        case 9: fixture.request.runtime_id = NEW_RUNTIME; strcpy(fixture.fake.status.runtime_id, NEW_RUNTIME); break;
        }
        pcm_body(&fixture, body, sizeof(body));
        response = pcm_handle(&fixture, 409);
        TEST_ASSERT_INT_EQ(0, (int)response.accepted_frames);
        TEST_ASSERT_INT_EQ(0, (int)fixture.fake.sends);
        if (scenario == 7 || scenario == 8)
            TEST_ASSERT_INT_EQ(100, (int)response.next_sequence);
        pcm_assert_record(&fixture, RECORD);
    }
    pcm_fixture_clear(&fixture);
}

/* All samples vary by frame; boundary values also exercise signed LE decoding. */
static void pcm_pattern(uint8_t body[1600], int16_t expected[5][160]) {
    for (size_t frame = 0; frame < 5; frame++) {
        for (size_t sample = 0; sample < 160; sample++) {
            int16_t value = (int16_t)(-12345 + (int)frame * 1000 + (int)sample);
            if (sample == 0) value = INT16_MIN;
            if (sample == 1) value = INT16_MAX;
            if (sample == 2) value = -1;
            if (sample == 3) value = 0;
            if (sample == 4) value = 1;
            expected[frame][sample] = value;
            uint16_t raw = (uint16_t)value;
            size_t offset = frame * 320 + sample * 2;
            body[offset] = (uint8_t)raw;
            body[offset + 1] = (uint8_t)(raw >> 8);
        }
    }
}

static void pcm_test_pacing_and_recovery(void) {
    uint8_t body[1600];
    int16_t expected[5][160];
    static const char *const lengths[] = {"320", "640", "960", "1280", "1600"};
    pcm_pattern(body, expected);
    for (unsigned frames = 1; frames <= 5; frames++) {
        struct pcm_fixture fixture;
        pcm_submit_init(&fixture);
        fixture.request.content_length = lengths[frames - 1];
        fixture.fake.send_duration = 7;
        pcm_body(&fixture, body, frames * 320);
        struct df_pcm_http_response response = pcm_handle(&fixture, 200);
        TEST_ASSERT_INT_EQ((int)frames, (int)response.accepted_frames);
        TEST_ASSERT_INT_EQ((int)(100 + frames), (int)response.next_sequence);
        TEST_ASSERT_INT_EQ((int)frames, (int)fixture.fake.sends);
        TEST_ASSERT_INT_EQ((int)frames - 1, (int)fixture.fake.sleeps);
        for (unsigned i = 0; i < frames; i++) {
            TEST_ASSERT_INT_EQ(1000 + (int)i * 20, (int)fixture.fake.send_times[i]);
            if (i != 0) TEST_ASSERT_INT_EQ(1000 + (int)i * 20, (int)fixture.fake.deadlines[i - 1]);
            TEST_ASSERT_INT_EQ(0, memcmp(expected[i], fixture.fake.samples[i], sizeof(expected[i])));
            TEST_ASSERT_INT_EQ(100 + (int)i, (int)fixture.fake.persisted_sequence[i]);
            TEST_ASSERT_INT_EQ(i == 0 ? 3000 : 3007 + ((int)i - 1) * 20,
                               (int)fixture.fake.persisted_lease[i]);
        }
        char record[256];
        snprintf(record, sizeof(record), "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=%u\naudio_session=" TOKEN "\nlease_deadline_ms=%u\n", 100 + frames, 3007 + (frames - 1) * 20);
        pcm_assert_record(&fixture, record);
        pcm_fixture_clear(&fixture);
    }
    struct pcm_fixture fixture;
    pcm_submit_init(&fixture);
    fixture.request.content_length = "1600";
    fixture.fake.fail_send = 3;
    pcm_body(&fixture, body, sizeof(body));
    struct df_pcm_http_response response = pcm_handle(&fixture, 503);
    TEST_ASSERT_INT_EQ(2, (int)response.accepted_frames);
    TEST_ASSERT_INT_EQ(102, (int)response.next_sequence);
    TEST_ASSERT_INT_EQ(3, (int)fixture.fake.sends);
    pcm_assert_record(&fixture, "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=102\naudio_session=" TOKEN "\nlease_deadline_ms=3020\n");
    /* An ambiguous/lost response may replay the old batch; it must send nothing. */
    pcm_body(&fixture, body, sizeof(body));
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, (int)response.accepted_frames);
    TEST_ASSERT_INT_EQ(102, (int)response.next_sequence);
    TEST_ASSERT_INT_EQ(3, (int)fixture.fake.sends);
    fixture.request.sequence = "102";
    fixture.request.content_length = "960";
    pcm_body(&fixture, body + 640, 960);
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(3, (int)response.accepted_frames);
    TEST_ASSERT_INT_EQ(105, (int)response.next_sequence);
    TEST_ASSERT_INT_EQ(6, (int)fixture.fake.sends);
    for (unsigned i = 0; i < 3; i++)
        TEST_ASSERT_INT_EQ(0, memcmp(expected[i + 2], fixture.fake.samples[i + 3], sizeof(expected[0])));
    pcm_assert_record(&fixture, "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=105\naudio_session=" TOKEN "\nlease_deadline_ms=3080\n");
    pcm_fixture_clear(&fixture);
}

static void pcm_test_submit_provider_failure(void) {
    uint8_t body[1600] = {0};
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        struct pcm_fixture fixture;
        pcm_submit_init(&fixture);
        fixture.request.content_length = "1600";
        if (scenario == 0) fixture.fake.fail_send = 1;
        if (scenario == 1) fixture.fake.fail_sleep = 2;
        if (scenario == 2) fixture.fake.status_result = DF_ERR_IO;
        pcm_body(&fixture, body, sizeof(body));
        struct df_pcm_http_response response = pcm_handle(&fixture, 503);
        TEST_ASSERT_INT_EQ(scenario == 1 ? 2 : 0, (int)response.accepted_frames);
        TEST_ASSERT_INT_EQ(scenario == 2 ? 0 : scenario == 1 ? 102 : 100, (int)response.next_sequence);
        TEST_ASSERT_INT_EQ(scenario == 2 ? 0 : scenario == 1 ? 2 : 1, (int)fixture.fake.sends);
        if (scenario != 1) pcm_assert_record(&fixture, RECORD);
        pcm_fixture_clear(&fixture);
    }
}

void test_pcm_http_validates_session_requests(void) {
    struct pcm_fixture fixture;
    static const char *const bad_methods[] = {NULL, "", "GET", "post", "POST ", "POST\n"};
    static const char *const bad_runtime[] = {NULL, "", "0123456789abcde", "0123456789abcdef0", "0123456789abcdeF", "g123456789abcdef"};
    static const char *const bad_generation[] = {NULL, "", "0", "+42", "-42", " 42", "42 ", "42x", "18446744073709551616"};
    static const char *const bad_length[] = {NULL, "", "1", "-0", "+0", "0 ", "18446744073709551616"};
    pcm_fixture_init(&fixture);
    for (size_t i = 0; i < sizeof(bad_methods) / sizeof(*bad_methods); i++) {
        fixture.request.method = bad_methods[i];
        (void)pcm_handle(&fixture, 405);
    }
    fixture.request.method = "POST";
    for (size_t i = 0; i < sizeof(bad_runtime) / sizeof(*bad_runtime); i++) {
        fixture.request.runtime_id = bad_runtime[i];
        (void)pcm_handle(&fixture, 400);
    }
    fixture.request.runtime_id = RUNTIME;
    for (size_t i = 0; i < sizeof(bad_generation) / sizeof(*bad_generation); i++) {
        fixture.request.generation = bad_generation[i];
        (void)pcm_handle(&fixture, 400);
    }
    fixture.request.generation = "42";
    for (size_t i = 0; i < sizeof(bad_length) / sizeof(*bad_length); i++) {
        fixture.request.content_length = bad_length[i];
        (void)pcm_handle(&fixture, 400);
    }
    fixture.request.content_length = "0";
    TEST_ASSERT_INT_EQ(-1, access(fixture.path, F_OK));
    pcm_fixture_clear(&fixture);
    /* Keep registration in the existing entry point: Task 3 touches two files. */
    pcm_test_submit_validation();
    pcm_test_body_admission();
    pcm_test_submit_admission();
    pcm_test_pacing_and_recovery();
    pcm_test_submit_provider_failure();
}

void test_pcm_http_requires_authoritative_talking_transmitter(void) {
    struct pcm_fixture fixture;
    struct df_pcm_http_response response;
    pcm_fixture_init(&fixture);
    strcpy(fixture.fake.status.runtime_id, NEW_RUNTIME);
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp(NEW_RUNTIME, response.runtime_id));
    TEST_ASSERT_INT_EQ(42, (int)response.generation);
    strcpy(fixture.fake.status.runtime_id, RUNTIME);
    fixture.fake.status.generation = 43;
    (void)pcm_handle(&fixture, 409);
    fixture.fake.status.generation = 42;
    strcpy(fixture.fake.status.call_state, "ringing");
    (void)pcm_handle(&fixture, 409);
    strcpy(fixture.fake.status.call_state, "talking");
    fixture.fake.status.audio_tx_active = false;
    (void)pcm_handle(&fixture, 409);
    fixture.fake.status.audio_tx_active = true;
    fixture.fake.status.audio_tx_generation = 41;
    (void)pcm_handle(&fixture, 409);
    pcm_assert_record(&fixture, "");
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_opens_and_releases_producer_lease(void) {
    struct pcm_fixture fixture;
    struct df_pcm_http_response response;
    struct stat statbuf;
    pcm_fixture_init(&fixture);
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(0, strcmp("", response.error));
    TEST_ASSERT_INT_EQ(0, strcmp(RUNTIME, response.runtime_id));
    TEST_ASSERT_INT_EQ(0, strcmp(TOKEN, response.audio_session));
    TEST_ASSERT_INT_EQ(42, (int)response.generation);
    TEST_ASSERT_INT_EQ(0, (int)response.accepted_frames);
    TEST_ASSERT_INT_EQ(0, (int)response.next_sequence);
    TEST_ASSERT_INT_EQ(2000, (int)response.lease_ms);
    TEST_ASSERT_INT_EQ(0, stat(fixture.path, &statbuf));
    TEST_ASSERT_INT_EQ(0600, statbuf.st_mode & 07777);
    TEST_ASSERT_INT_EQ(1, statbuf.st_uid == geteuid());
    pcm_assert_record(&fixture, "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=0\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n");
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp("producer_busy", response.error));
    TEST_ASSERT_INT_EQ(0, strcmp("", response.audio_session));
    fixture.request.operation = DF_PCM_HTTP_SESSION_END;
    fixture.request.session_token = "ffffffffffffffffffffffffffffffff";
    (void)pcm_handle(&fixture, 409);
    fixture.request.session_token = "000102030405060708090a0b0c0d0e0F";
    (void)pcm_handle(&fixture, 400);
    fixture.request.session_token = NULL;
    (void)pcm_handle(&fixture, 400);
    pcm_write_record(&fixture, RECORD);
    fixture.request.session_token = TOKEN;
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(100, (int)response.next_sequence);
    pcm_assert_record(&fixture, "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\naudio_session=\nlease_deadline_ms=0\n");
    fixture.request.operation = DF_PCM_HTTP_SESSION_OPEN;
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(100, (int)response.next_sequence);
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_expires_and_preserves_sequence(void) {
    struct pcm_fixture fixture;
    struct df_pcm_http_response response;
    pcm_fixture_init(&fixture);
    pcm_write_record(&fixture, RECORD);
    fixture.fake.now = 2999;
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp("producer_busy", response.error));
    fixture.fake.now = 3000;
    fixture.request.operation = DF_PCM_HTTP_SESSION_END;
    fixture.request.session_token = TOKEN;
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp("session_expired", response.error));
    pcm_assert_record(&fixture, RECORD);
    fixture.request.operation = DF_PCM_HTTP_SESSION_OPEN;
    fixture.fake.random_seed = 16;
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(100, (int)response.next_sequence);
    TEST_ASSERT_INT_EQ(0, strcmp("101112131415161718191a1b1c1d1e1f", response.audio_session));
    fixture.request.operation = DF_PCM_HTTP_SESSION_END;
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp("", response.audio_session));
    fixture.request.operation = DF_PCM_HTTP_SESSION_OPEN;
    response = pcm_handle(&fixture, 409);
    TEST_ASSERT_INT_EQ(0, strcmp("producer_busy", response.error));
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_only_open_can_replace_runtime_or_generation(void) {
    struct pcm_fixture fixture;
    struct df_pcm_http_response response;
    pcm_fixture_init(&fixture);
    pcm_write_record(&fixture, RECORD);
    strcpy(fixture.fake.status.runtime_id, NEW_RUNTIME);
    fixture.request.runtime_id = NEW_RUNTIME;
    fixture.request.operation = DF_PCM_HTTP_SESSION_END;
    fixture.request.session_token = TOKEN;
    (void)pcm_handle(&fixture, 409);
    pcm_assert_record(&fixture, RECORD);
    fixture.request.operation = DF_PCM_HTTP_SESSION_OPEN;
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(0, (int)response.next_sequence);
    pcm_write_record(&fixture, RECORD);
    strcpy(fixture.fake.status.runtime_id, RUNTIME);
    fixture.request.runtime_id = RUNTIME;
    fixture.fake.status.generation = UINT64_MAX;
    fixture.fake.status.audio_tx_generation = UINT64_MAX;
    fixture.request.generation = "18446744073709551615";
    fixture.request.operation = DF_PCM_HTTP_SESSION_END;
    (void)pcm_handle(&fixture, 409);
    pcm_assert_record(&fixture, RECORD);
    fixture.request.operation = DF_PCM_HTTP_SESSION_OPEN;
    response = pcm_handle(&fixture, 200);
    TEST_ASSERT_INT_EQ(1, response.generation == UINT64_MAX);
    TEST_ASSERT_INT_EQ(0, (int)response.next_sequence);
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_rejects_unsafe_and_malformed_state(void) {
    struct pcm_fixture fixture;
    static const char *const invalid_records[] = {
        "runtime_id=" RUNTIME "\n",
        RECORD "generation=42\n",
        RECORD "unknown=1\n",
        "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=3000",
        "runtime_id=" RUNTIME "\ngeneration=+42\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=0\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=18446744073709551616\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\naudio_session=bad\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\naudio_session=\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=42\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=0\n",
        "runtime_id=0123456789abcdeF\ngeneration=42\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n",
        "runtime_id=" RUNTIME "\ngeneration=000000000000000000042\nnext_sequence=100\naudio_session=" TOKEN "\nlease_deadline_ms=3000\n",
        RECORD "\n"
    };
    pcm_fixture_init(&fixture);
    TEST_ASSERT_INT_EQ(0, symlink("missing-target", fixture.path));
    (void)pcm_handle(&fixture, 503);
    TEST_ASSERT_INT_EQ(0, unlink(fixture.path));
    TEST_ASSERT_INT_EQ(0, mkdir(fixture.path, 0700));
    (void)pcm_handle(&fixture, 503);
    TEST_ASSERT_INT_EQ(0, rmdir(fixture.path));
    TEST_ASSERT_INT_EQ(0, mkfifo(fixture.path, 0600));
    (void)pcm_handle(&fixture, 503);
    TEST_ASSERT_INT_EQ(0, unlink(fixture.path));
    pcm_write_record(&fixture, RECORD);
    TEST_ASSERT_INT_EQ(0, chmod(fixture.path, 0644));
    (void)pcm_handle(&fixture, 503);
    pcm_assert_record(&fixture, RECORD);
    TEST_ASSERT_INT_EQ(0, chmod(fixture.path, 0600));
    for (size_t i = 0; i < sizeof(invalid_records) / sizeof(*invalid_records); i++) {
        pcm_write_record(&fixture, invalid_records[i]);
        (void)pcm_handle(&fixture, 503);
        pcm_assert_record(&fixture, invalid_records[i]);
    }
    char oversized[1024];
    memset(oversized, 'x', sizeof(oversized) - 1U);
    oversized[sizeof(oversized) - 1U] = '\0';
    pcm_write_record(&fixture, oversized);
    (void)pcm_handle(&fixture, 503);
    pcm_assert_record(&fixture, oversized);
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_provider_failures_do_not_change_state(void) {
    struct pcm_fixture fixture;
    pcm_fixture_init(&fixture);
    pcm_write_record(&fixture, RECORD);
    fixture.fake.status_result = DF_ERR_IO;
    (void)pcm_handle(&fixture, 503);
    pcm_assert_record(&fixture, RECORD);
    fixture.fake.status_result = DF_OK;
    fixture.fake.now = 3000;
    fixture.fake.random_result = DF_ERR_IO;
    (void)pcm_handle(&fixture, 503);
    pcm_assert_record(&fixture, RECORD);
    fixture.fake.random_result = DF_OK;
    fixture.fake.now = UINT64_MAX - 1999U;
    (void)pcm_handle(&fixture, 503);
    pcm_assert_record(&fixture, RECORD);
    pcm_fixture_clear(&fixture);
}

void test_pcm_http_stale_open_waiting_on_lock_cannot_revert_state(void) {
    struct pcm_fixture fixture;
    struct df_pcm_http_response response;
    int ready[2], result[2], status;
    char byte;
    pcm_fixture_init(&fixture);
    pcm_write_record(&fixture, RECORD);
    struct pcm_fake *shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANON, -1, 0);
    TEST_ASSERT_INT_EQ(1, shared != MAP_FAILED);
    if (shared == MAP_FAILED) { pcm_fixture_clear(&fixture); return; }
    *shared = fixture.fake;
    fixture.config.ops.context = shared;
    int lock_fd = open(fixture.path, O_RDWR);
    TEST_ASSERT_INT_EQ(1, lock_fd >= 0);
    TEST_ASSERT_INT_EQ(0, flock(lock_fd, LOCK_EX));
    TEST_ASSERT_INT_EQ(0, pipe(ready));
    TEST_ASSERT_INT_EQ(0, pipe(result));
    pid_t child = fork();
    TEST_ASSERT_INT_EQ(1, child >= 0);
    if (child == 0) {
        (void)alarm(5);
        close(lock_fd);
        close(ready[0]);
        close(result[0]);
        if (write(ready[1], "r", 1) != 1) _exit(2);
        int rc = df_pcm_http_handle(&fixture.config, &fixture.request, &response);
        if (write(result[1], &response, sizeof(response)) != (ssize_t)sizeof(response)) _exit(3);
        _exit(rc == DF_OK ? 0 : 4);
    }
    close(ready[1]);
    close(result[1]);
    TEST_ASSERT_INT_EQ(1, (int)read(ready[0], &byte, 1));
    struct pollfd pending = {.fd = result[0], .events = POLLIN};
    /* The request has started but cannot complete while the parent owns the lock. */
    TEST_ASSERT_INT_EQ(0, poll(&pending, 1, 100));
    strcpy(shared->status.runtime_id, NEW_RUNTIME);
    pcm_write_record(&fixture, "runtime_id=" NEW_RUNTIME "\ngeneration=42\nnext_sequence=101\naudio_session=" TOKEN "\nlease_deadline_ms=9000\n");
    TEST_ASSERT_INT_EQ(0, flock(lock_fd, LOCK_UN));
    close(lock_fd);
    TEST_ASSERT_INT_EQ(1, poll(&pending, 1, 5000));
    TEST_ASSERT_INT_EQ((int)sizeof(response), (int)read(result[0], &response, sizeof(response)));
    TEST_ASSERT_INT_EQ(409, (int)response.http_status);
    TEST_ASSERT_INT_EQ(0, strcmp(NEW_RUNTIME, response.runtime_id));
    TEST_ASSERT_INT_EQ(child, waitpid(child, &status, 0));
    TEST_ASSERT_INT_EQ(1, WIFEXITED(status));
    if (WIFEXITED(status)) TEST_ASSERT_INT_EQ(0, WEXITSTATUS(status));
    pcm_assert_record(&fixture, "runtime_id=" NEW_RUNTIME "\ngeneration=42\nnext_sequence=101\naudio_session=" TOKEN "\nlease_deadline_ms=9000\n");
    close(ready[0]);
    close(result[0]);
    TEST_ASSERT_INT_EQ(0, munmap(shared, sizeof(*shared)));
    pcm_fixture_clear(&fixture);
}
