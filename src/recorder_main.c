#include "evidence_recorder.h"
#include "recorder_selftest.h"
#include "evidence_metadata.h"
#include "deployment_snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#ifdef DF_RECORDER_PROGRAM

static volatile sig_atomic_t stopping;
static void stop_recording(int signal_number) { (void)signal_number; stopping = 1; }
static int available_space(void *context, uint64_t *bytes) {
    struct statvfs s;
    if (statvfs(context, &s) || !s.f_frsize ||
        s.f_bavail > UINT64_MAX / s.f_frsize) return DF_ERR_IO;
    *bytes = (uint64_t)s.f_bavail * s.f_frsize;
    return DF_OK;
}
static int private_directory(const char *path) {
    struct stat s;
    if (mkdir(path, 0700) && errno != EEXIST) return DF_ERR_IO;
    if (lstat(path, &s) || !S_ISDIR(s.st_mode) || s.st_uid != geteuid() ||
        (s.st_mode & 0777) != 0700) return DF_ERR_INVALID;
    return DF_OK;
}
static int write_status(const struct df_evidence_recorder *r, const char *state) {
    char temporary[] = "/var/run/doorfast-recorder.status.XXXXXX";
    struct df_pcap_ring_status recent, control;
    if (df_pcap_ring_status(r->recent, &recent) ||
        df_pcap_ring_status(r->control, &control)) return DF_ERR_IO;
    int fd = mkstemp(temporary);
    if (fd < 0) return DF_ERR_IO;
    FILE *output = fdopen(fd, "w");
    if (!output) { close(fd); unlink(temporary); return DF_ERR_IO; }
    int failed = fprintf(output,
        "{\"schema_version\":1,\"pid\":%ld,\"state\":\"%s\","
        "\"updated_wall_seconds\":%" PRIu64 ","
        "\"packets_seen\":%" PRIu64 ",\"recent_packets\":%" PRIu64 ","
        "\"control_packets\":%" PRIu64 ",\"invalid_packets\":%" PRIu64 ","
        "\"recent_bytes\":%" PRIu64 ",\"control_bytes\":%" PRIu64 ","
        "\"available_bytes\":%" PRIu64 ",\"reserve_bytes\":%" PRIu64 "}\n",
        (long)getpid(), state, (uint64_t)time(NULL), r->packets_seen,
        r->recent_packets, r->control_packets, r->invalid_packets,
        recent.bytes_written, control.bytes_written,
        r->available_bytes, r->reserve_bytes) < 0;
    if (fflush(output)) failed = 1;
    if (fclose(output)) failed = 1;
    if (!failed && rename(temporary, "/var/run/doorfast-recorder.status") == 0)
        return DF_OK;
    unlink(temporary);
    return DF_ERR_IO;
}
int main(int argc, char **argv) {
    struct df_deployment_config config;
    struct df_deployment_snapshot snapshot;
    struct df_deployment_report report;
    struct df_pcap_ring recent = {0}, control = {0};
    struct df_evidence_recorder recorder;
    struct df_evidence_log log = {0};
    struct df_capture *capture = NULL;
    char contents[65537] = {0};
    int result = 1, lock = -1;
    FILE *file;
    size_t length;
    if (argc == 3 && !strcmp(argv[1], "--self-test"))
        return df_recorder_selftest(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: doorfast-recorder --config /etc/config/doorfast-deployment");
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--config") ||
        strcmp(argv[2], "/etc/config/doorfast-deployment")) return 2;
    file = fopen(argv[2], "rb");
    if (!file) return 1;
    length = fread(contents, 1, sizeof(contents) - 1, file);
    int failed = ferror(file) || (length == sizeof(contents) - 1 && fgetc(file) != EOF);
    if (fclose(file)) failed = 1;
    if (failed || df_deployment_config_parse(contents, &config)) return 2;
    if (!config.enabled || !config.recording_enabled) return 0;
    if (df_deployment_snapshot_collect(&config, "/", &snapshot) ||
        df_deployment_preflight_evaluate(&config, &snapshot, &report) || !report.safe)
        return 2;
    /* Fixed deployment limits; reject profiles the recorder cannot honor. */
    if (config.recent_budget_mib != 14336 || config.control_budget_mib != 8192 ||
        config.log_budget_mib != 1024) return 2;
    lock = open("/mnt/doorfast/recorder.lock", O_WRONLY | O_CREAT | O_NOFOLLOW, 0600);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB)) goto done;
    if (private_directory("/mnt/doorfast/recent") ||
        private_directory("/mnt/doorfast/control") ||
        private_directory("/mnt/doorfast/logs")) goto done;
    const struct df_evidence_log_config log_config = {
        .directory = "/mnt/doorfast/logs", .prefix = "events",
        .segment_count = 63, .segment_bytes = UINT64_C(16) * 1024 * 1024};
    if (df_evidence_log_open(&log, &log_config)) goto done;
    /* Reserve one slot worth of budget for the concurrently active partial. */
    struct df_pcap_ring_config ring = {.directory = "/mnt/doorfast/recent",
        .prefix = "recent", .segment_count = 111,
        .segment_bytes = UINT64_C(128) * 1024 * 1024, .snaplen = 256};
    if (df_pcap_ring_init(&recent, &ring)) goto done;
    ring.directory = "/mnt/doorfast/control"; ring.prefix = "control";
    ring.segment_count = 63; ring.snaplen = 2048;
    if (df_pcap_ring_init(&control, &ring) ||
        df_evidence_recorder_init(&recorder, &recent, &control,
            (uint64_t)config.reserve_mib * 1024 * 1024,
            available_space, config.evidence_root) ||
        df_capture_open(config.bridge, true, &capture) ||
        df_capture_set_filter(capture, df_capture_default_filter())) goto done;
    signal(SIGTERM, stop_recording); signal(SIGINT, stop_recording);
    if (available_space(config.evidence_root, &recorder.available_bytes) ||
        write_status(&recorder, "recording")) goto done;
    result = 0;
    time_t last_status = time(NULL);
    while (!stopping) {
        time_t now = time(NULL);
        if (now != last_status) {
            if (available_space(config.evidence_root, &recorder.available_bytes) ||
                write_status(&recorder, "recording")) { result = 1; break; }
            last_status = now;
        }
        struct df_capture_record packet;
        int next = df_capture_next_record(capture, &packet);
        if (next == DF_CAPTURE_TIMEOUT) {
            const struct timespec delay = {.tv_nsec = 10000000};
            nanosleep(&delay, NULL);
            continue;
        }
        uint64_t previous_control = recorder.control_packets;
        if (next == DF_CAPTURE_ERROR || df_evidence_recorder_accept(&recorder, &packet)) {
            result = 1; break;
        }
        if (recorder.control_packets != previous_control) {
            struct timespec monotonic;
            uint64_t free_bytes;
            if (available_space(config.evidence_root, &free_bytes) ||
                clock_gettime(CLOCK_MONOTONIC, &monotonic)) { result = 1; break; }
            if (free_bytes < recorder.reserve_bytes ||
                free_bytes - recorder.reserve_bytes < 4096) {
                recorder.state = DF_RECORDER_SPACE_GUARD;
            } else if (df_evidence_metadata_append(&log, &packet, config.bridge,
                (uint64_t)monotonic.tv_sec * 1000 + (uint64_t)monotonic.tv_nsec / 1000000)) {
                result = 1; break;
            }
        }
        if (recorder.state == DF_RECORDER_SPACE_GUARD) {
            fputs("doorfast-recorder: space_guard\n", stderr);
            break;
        }
    }
    if (write_status(&recorder, result ? "io_error" :
        recorder.state == DF_RECORDER_SPACE_GUARD ? "space_guard" : "stopped"))
        result = 1;
done:
    df_capture_close(capture);
    if (log.file && df_evidence_log_close(&log)) result = 1;
    if (recent.file && df_pcap_ring_close(&recent)) result = 1;
    if (control.file && df_pcap_ring_close(&control)) result = 1;
    if (lock >= 0) close(lock);
    return result;
}
#endif
