#ifndef DOORFAST_PCM_HTTP_H
#define DOORFAST_PCM_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_PCM_HTTP_FRAME_BYTES 320U
#define DF_PCM_HTTP_MAX_FRAMES 5U
#define DF_PCM_HTTP_LEASE_MS 2000U

enum df_pcm_http_operation {
    DF_PCM_HTTP_SESSION_OPEN,
    DF_PCM_HTTP_SUBMIT,
    DF_PCM_HTTP_SESSION_END
};

struct df_pcm_http_status {
    char runtime_id[17];
    char call_state[16];
    uint64_t generation;
    bool audio_tx_active;
    uint64_t audio_tx_generation;
};

struct df_pcm_http_request {
    enum df_pcm_http_operation operation;
    const char *method;
    const char *runtime_id;
    const char *generation;
    const char *sequence;
    const char *session_token;
    const char *content_type;
    const char *content_length;
    int body_fd;
};

struct df_pcm_http_response {
    unsigned http_status;
    char error[32];
    char runtime_id[17];
    char audio_session[33];
    uint64_t generation;
    uint64_t accepted_frames;
    uint64_t next_sequence;
    uint64_t lease_ms;
};

struct df_pcm_http_ops {
    int (*read_status)(struct df_pcm_http_status *, void *);
    /* Return zero only after filling every requested byte. */
    int (*fill_random)(uint8_t *, size_t, void *);
    uint64_t (*now_ms)(void *);
    int (*sleep_until_ms)(uint64_t, void *);
    int (*send_pcm)(const char *, uint64_t, const int16_t *, size_t, void *);
    void *context;
};

struct df_pcm_http_config {
    const char *state_path;
    const char *socket_path;
    struct df_pcm_http_ops ops;
};

/* read_status and monotonic now_ms are required; open also needs fill_random.
 * DF_OK means response contains an HTTP result, including HTTP errors.
 * Invalid API arguments return DF_ERR_INVALID. SUBMIT is not implemented yet.
 */
int df_pcm_http_handle(const struct df_pcm_http_config *,
    const struct df_pcm_http_request *, struct df_pcm_http_response *);

#endif
