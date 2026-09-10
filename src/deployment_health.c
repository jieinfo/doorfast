#include "deployment_health.h"
#include "deployment_snapshot.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int literal(const char **cursor, const char *text) {
    size_t n = strlen(text);
    if (strncmp(*cursor, text, n)) return 0;
    *cursor += n;
    return 1;
}
static int decimal(const char **cursor, uint64_t *out) {
    uint64_t value = 0;
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p; *out = value; return 1;
}
static void recorder_health(const char *root, struct df_deployment_health *out) {
    char path[1024], buffer[2048], state[24] = {0};
    snprintf(path, sizeof(path), "%s/var/run/doorfast-recorder.status", root);
    FILE *file = fopen(path, "r");
    if (!file) return;
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
    int failed = ferror(file);
    if (fclose(file)) failed = 1;
    if (failed || n == sizeof(buffer) - 1) return;
    buffer[n] = 0;
    const char *p = buffer;
    uint64_t pid, values[12];
    if (!literal(&p, "{\"schema_version\":1,\"pid\":") || !decimal(&p, &pid) || !pid ||
        !literal(&p, ",\"state\":\"")) return;
    size_t s = 0;
    while (*p && *p != '"' && s + 1 < sizeof(state)) state[s++] = *p++;
    if (!literal(&p, "\"") || (strcmp(state, "recording") &&
        strcmp(state, "space_guard") && strcmp(state, "io_error") && strcmp(state, "stopped"))) return;
    const char *keys[] = {"updated_wall_seconds", "packets_seen", "recent_packets",
        "control_packets", "invalid_packets", "last_packet_wall_seconds",
        "last_rotation_wall_seconds", "recent_bytes", "control_bytes", "log_bytes",
        "available_bytes", "reserve_bytes"};
    for (unsigned i = 0; i < 12; ++i) {
        char key[80];
        snprintf(key, sizeof(key), ",\"%s\":", keys[i]);
        if (!literal(&p, key) || !decimal(&p, &values[i])) return;
    }
    if (!literal(&p, "}\n") || *p) return;
    out->recorder_present = true;
    uint64_t now = (uint64_t)time(NULL);
    if (!strcmp(state, "recording") && (values[0] > now || now - values[0] > 10))
        strcpy(state, "stale");
    strcpy(out->recorder_state, state);
    out->recent_bytes = values[7]; out->control_bytes = values[8];
    out->log_bytes = values[9]; out->available_bytes = values[10];
    out->reserve_bytes = values[11];
}

static int number(const char *path, uint64_t *out) {
    char buffer[64];
    FILE *file = fopen(path, "r");
    if (!file) return DF_ERR_IO;
    size_t n = fread(buffer, 1, sizeof(buffer), file);
    int failed = ferror(file);
    if (fclose(file)) failed = 1;
    if (failed || !n || n == sizeof(buffer)) return DF_ERR_IO;
    uint64_t value = 0;
    size_t i = 0;
    while (i < n && buffer[i] >= '0' && buffer[i] <= '9') {
        unsigned digit = (unsigned)(buffer[i++] - '0');
        if (value > (UINT64_MAX - digit) / 10) return DF_ERR_IO;
        value = value * 10 + digit;
    }
    if (!i || (i != n && !(i + 1 == n && buffer[i] == '\n'))) return DF_ERR_IO;
    *out = value;
    return DF_OK;
}
static void link_health(const char *root, const char *name, struct df_link_health *out) {
    char path[1024];
    struct stat s;
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(path, sizeof(path), "%s/sys/class/net/%s", root, name);
    if (stat(path, &s) || !S_ISDIR(s.st_mode)) return;
    out->present = true;
    snprintf(path, sizeof(path), "%s/sys/class/net/%s/carrier", root, name);
    uint64_t carrier;
    if (!number(path, &carrier) && carrier <= 1) {
        out->carrier_known = true; out->carrier = carrier == 1;
    }
    const char *keys[] = {"rx_packets", "tx_packets", "rx_dropped", "tx_dropped",
        "rx_errors", "tx_errors"};
    uint64_t values[6];
    for (unsigned i = 0; i < 6; ++i) {
        snprintf(path, sizeof(path), "%s/sys/class/net/%s/statistics/%s", root, name, keys[i]);
        if (number(path, &values[i])) return;
    }
    out->counters_known = true;
    out->rx_packets = values[0]; out->tx_packets = values[1];
    out->rx_dropped = values[2]; out->tx_dropped = values[3];
    out->rx_errors = values[4]; out->tx_errors = values[5];
}
int df_deployment_health_collect(const struct df_deployment_config *config,
    const char *root, struct df_deployment_health *out) {
    if (!root || !out || root[0] != '/' || strlen(root) > 512 ||
        df_deployment_config_validate(config)) return DF_ERR_INVALID;
    *out = (struct df_deployment_health){0};
    if (!config->enabled) return DF_OK;
    out->configured = true;
    recorder_health(root, out);
    snprintf(out->observation_interface, sizeof(out->observation_interface), "%s", config->observation);
    link_health(root, config->upstream, &out->upstream);
    link_health(root, config->downstream, &out->downstream);
    link_health(root, config->management, &out->management);
    struct df_deployment_snapshot snapshot;
    struct df_deployment_report report;
    if (!df_deployment_snapshot_collect(config, root, &snapshot)) {
        out->passive_only = snapshot.doorfast_passive_only;
        if (!df_deployment_preflight_evaluate(config, &snapshot, &report))
            out->preflight_safe = report.safe;
    }
    return DF_OK;
}

int df_deployment_health_load(struct df_deployment_health *out) {
    if (!out) return DF_ERR_INVALID;
    *out = (struct df_deployment_health){0};
    FILE *file = fopen("/etc/config/doorfast-deployment", "r");
    if (!file) return DF_ERR_IO;
    char text[65537];
    size_t n = fread(text, 1, sizeof(text) - 1, file);
    int failed = ferror(file) || n == sizeof(text) - 1;
    if (fclose(file)) failed = 1;
    text[n] = 0;
    struct df_deployment_config config;
    if (failed || df_deployment_config_parse(text, &config)) return DF_ERR_IO;
    return df_deployment_health_collect(&config, "/", out);
}
