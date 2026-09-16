#include "event_relay.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop_relay(int signal_number) { (void)signal_number; running = 0; }

static int post_event(const char *url, const char *entry, const char *token,
                      const char *ca_file, const struct df_relay_event *event) {
    char host[256], authority[256], port[16], path[512], request[4096];
    const int https = strncmp(url, "https://", 8) == 0;
    const char *p = url + (https ? 8 : 7);
    const char *slash = strchr(p, '/');
    int fd = -1, status = 0;
    struct addrinfo hints, *results = NULL, *item;
    const char *colon;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    size_t body_len = strlen(event->line), offset = 0;
    if ((slash && (size_t)(slash - p) >= sizeof(authority)) || (!slash && strlen(p) >= sizeof(authority))) return -1;
    if (slash) { memcpy(authority, p, (size_t)(slash - p)); authority[slash - p] = '\0'; }
    else strcpy(authority, p);
    strcpy(port, https ? "443" : "80");
    colon = strrchr(authority, ':');
    if (colon && strchr(colon + 1, ':') == NULL) {
        size_t host_len = (size_t)(colon - authority);
        if (host_len == 0 || host_len >= sizeof(host) || strlen(colon + 1) >= sizeof(port)) return -1;
        memcpy(host, authority, host_len); host[host_len] = '\0'; strcpy(port, colon + 1);
    } else strcpy(host, authority);
    if (snprintf(path, sizeof(path), "%s/api/doorfast/%s", slash ? slash : "", entry) >= (int)sizeof(path)) return -1;
    if (snprintf(request, sizeof(request),
                 "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
                 "Authorization: Bearer %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                 path, authority, token, body_len, event->line) >= (int)sizeof(request)) return -1;
    memset(&hints, 0, sizeof(hints)); hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host, port, &hints, &results)) return -1;
    for (item = results; item; item = item->ai_next) {
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
                int connected = connect(fd, item->ai_addr, item->ai_addrlen);
                if (connected == 0) break;
                if (connected < 0 && errno == EINPROGRESS) {
                    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                    int error = 0; socklen_t error_length = sizeof(error);
                    if (poll(&pfd, 1, 5000) > 0 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_length) && error == 0) break;
                }
            }
        }
        if (fd >= 0) { close(fd); fd = -1; }
    }
    freeaddrinfo(results);
    if (fd < 0) return -1;
    { int flags = fcntl(fd, F_GETFL, 0); if (flags >= 0) (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK); }
    { struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); }
    if (https) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx || ca_file == NULL || !SSL_CTX_load_verify_locations(ctx, ca_file, NULL)) goto done;
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        ssl = SSL_new(ctx);
        if (!ssl || !SSL_set_tlsext_host_name(ssl, host) || !SSL_set1_host(ssl, host) ||
            !SSL_set_fd(ssl, fd) || SSL_connect(ssl) != 1) goto done;
        while (offset < strlen(request)) {
            int written = SSL_write(ssl, request + offset, (int)(strlen(request) - offset));
            if (written <= 0) goto done;
            offset += (size_t)written;
        }
    } else {
        while (offset < strlen(request)) {
            ssize_t written = write(fd, request + offset, strlen(request) - offset);
            if (written <= 0) goto done;
            offset += (size_t)written;
        }
    }
    {
        char response[256] = {0};
        int n = https ? SSL_read(ssl, response, sizeof(response) - 1) :
            (int)read(fd, response, sizeof(response) - 1);
        if (n > 12 && sscanf(response, "HTTP/%*s %d", &status) != 1) status = 0;
    }
done:
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
    close(fd);
    return status;
}

static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address;
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address)); address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path)) { close(fd); return -1; }
    strcpy(address.sun_path, path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) { close(fd); return -1; }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return -1; }
    return fd;
}

int main(int argc, char **argv) {
    const char *socket_path = "/var/run/doorfast/events.sock", *url = NULL;
    const char *entry = NULL, *token_path = NULL, *ca_file = NULL;
    char token[4097], line[DF_RELAY_MAX_LINE];
    struct df_relay_queue queue; struct df_relay_event event;
    int socket_fd = -1; size_t used = 0; unsigned attempt = 0;
    int i;
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--socket")) socket_path = argv[i + 1];
        else if (!strcmp(argv[i], "--url")) url = argv[i + 1];
        else if (!strcmp(argv[i], "--entry-id")) entry = argv[i + 1];
        else if (!strcmp(argv[i], "--token-file")) token_path = argv[i + 1];
        else if (!strcmp(argv[i], "--ca-file")) ca_file = argv[i + 1];
        else return 2;
    }
    if (!url || !entry || !token_path || df_relay_validate_url(url) ||
        df_relay_validate_entry_id(entry) || df_relay_token_file_ok(token_path)) return 2;
    {
        FILE *file = fopen(token_path, "r"); size_t n;
        if (!file || (n = fread(token, 1, sizeof(token) - 1, file)) == 0) return 2;
        fclose(file); token[n] = '\0';
        while (n && (token[n - 1] == '\n' || token[n - 1] == '\r')) token[--n] = '\0';
        if (!n) return 2;
        for (size_t j = 0; j < n; j++) {
            unsigned char c = (unsigned char)token[j];
            if (c < 0x21 || c == 0x7f) return 2;
        }
    }
    signal(SIGTERM, stop_relay); signal(SIGINT, stop_relay); signal(SIGPIPE, SIG_IGN); SSL_library_init();
    df_relay_queue_init(&queue);
    while (running) {
        if (socket_fd < 0) { socket_fd = connect_unix(socket_path); if (socket_fd < 0) { sleep(1); continue; } }
        if (used == sizeof(line) - 1) { used = 0; continue; }
        { ssize_t n = read(socket_fd, line + used, sizeof(line) - 1 - used);
          int would_block = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
          if (would_block) n = 0;
          if (n < 0 || (n == 0 && !would_block)) { close(socket_fd); socket_fd = -1; used = 0; continue; }
          used += (size_t)n;
          if (n == 0 && queue.length == 0) usleep(10000); }
        while (used) {
            char *newline = memchr(line, '\n', used); size_t length;
            if (!newline) break;
            length = (size_t)(newline - line) + 1;
            if (!df_relay_parse_event(line, length, &event)) df_relay_queue_push(&queue, &event);
            memmove(line, line + length, used - length); used -= length;
        }
        if (df_relay_queue_peek(&queue, &event) == 0) {
            int status = post_event(url, entry, token, ca_file, &event);
            if (status >= 200 && status < 300) { df_relay_queue_pop(&queue, &event); attempt = 0; }
            else if (status > 0 && !df_relay_retryable_status(status)) { df_relay_queue_pop(&queue, &event); attempt = 0; }
            else { usleep((useconds_t)(df_relay_backoff_ms(attempt++, 60000) * 1000)); }
        }
    }
    if (socket_fd >= 0) close(socket_fd);
    OPENSSL_cleanup();
    return 0;
}
