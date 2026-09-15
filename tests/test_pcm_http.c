#include <errno.h>
#include <fcntl.h>
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
    fixture->config.ops.read_status = pcm_read_status;
    fixture->config.ops.fill_random = pcm_fill_random;
    fixture->config.ops.now_ms = pcm_now;
    fixture->config.ops.context = &fixture->fake;
    fixture->request.operation = DF_PCM_HTTP_SESSION_OPEN;
    fixture->request.method = "POST";
    fixture->request.runtime_id = RUNTIME;
    fixture->request.generation = "42";
    fixture->request.content_length = "0";
    fixture->request.body_fd = -1;
}

static void pcm_fixture_clear(struct pcm_fixture *fixture) {
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
    fixture.request.operation = DF_PCM_HTTP_SUBMIT;
    (void)pcm_handle(&fixture, 501);
    TEST_ASSERT_INT_EQ(-1, access(fixture.path, F_OK));
    pcm_fixture_clear(&fixture);
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
