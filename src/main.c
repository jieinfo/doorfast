#include "config.h"
#include "deployment_config.h"
#include "deployment_preflight.h"
#include "deployment_report.h"
#include "deployment_snapshot.h"
#include "gvs_identity.h"
#include "gvs_replay.h"
#include "runtime_config.h"
#include "runtime_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DF_RECORDER_PROGRAM

#define DF_MAX_LEGACY_CONFIG_BYTES 65536

static void df_print_usage(FILE *stream) {
    (void)fputs("Usage: doorfast --help | --config <path> | --preflight <path> | --import-legacy <path> | --inspect-pcap <path> <IS-address> | --simulate-handshake-pcap <path> <IS-address>\n", stream);
}

static int df_read_deployment_config(const char *path,
                                     struct df_deployment_config *config) {
    char *contents;
    FILE *file;
    size_t length;
    int result;

    file = fopen(path, "rb");
    if (file == NULL) return DF_ERR_IO;
    contents = calloc(65537, 1);
    if (contents == NULL) {
        (void)fclose(file);
        return DF_ERR_IO;
    }
    length = fread(contents, 1, 65536, file);
    if (ferror(file) || (length == 65536 && fgetc(file) != EOF) ||
        fclose(file) != 0) {
        free(contents);
        return DF_ERR_IO;
    }
    result = df_deployment_config_parse(contents, config);
    free(contents);
    return result;
}

static int df_run_preflight(const char *path, const char *root) {
    struct df_deployment_config config;
    struct df_deployment_snapshot snapshot;
    struct df_deployment_report report;
    int result = df_read_deployment_config(path, &config);

    if (result == DF_ERR_INVALID) {
        (void)fputs("doorfast: invalid deployment configuration\n", stderr);
        return 2;
    }
    if (result != DF_OK ||
        df_deployment_snapshot_collect(&config, root, &snapshot) != DF_OK) {
        (void)fputs("doorfast: cannot collect deployment state\n", stderr);
        return 1;
    }
    if (df_deployment_preflight_evaluate(&config, &snapshot, &report) != DF_OK ||
        df_deployment_report_write_json(stdout, &config, &report) != DF_OK) {
        (void)fputs("doorfast: cannot evaluate deployment state\n", stderr);
        return 1;
    }
    return report.safe ? 0 : 2;
}

static int df_run_config(const char *path) {
    struct df_runtime_config runtime;
    int result = df_runtime_config_load(path, &runtime);

    if (result != DF_OK) {
        (void)fprintf(stderr, "doorfast: invalid or unreadable configuration: %s\n", path);
        return 2;
    }
    if (!runtime.config.enabled) {
        (void)fputs("doorfast: disabled\n", stdout);
        return 0;
    }
    result = df_runtime_service_run(&runtime);
    if (result != DF_OK) {
        (void)fprintf(stderr, "doorfast: runtime service failed on interface: %s\n",
                      runtime.config.gvs_interface);
        return 2;
    }
    return 0;
}

static int df_import_legacy_file(const char *path) {
    char *contents;
    FILE *file;
    size_t length;
    struct df_legacy_import imported;

    file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr, "doorfast: cannot read legacy configuration: %s\n", path);
        return 2;
    }
    contents = calloc(DF_MAX_LEGACY_CONFIG_BYTES + 1, sizeof(*contents));
    if (contents == NULL) {
        (void)fclose(file);
        (void)fputs("doorfast: cannot allocate configuration buffer\n", stderr);
        return 2;
    }
    length = fread(contents, 1, DF_MAX_LEGACY_CONFIG_BYTES, file);
    if (ferror(file) || (length == DF_MAX_LEGACY_CONFIG_BYTES && fgetc(file) != EOF)) {
        free(contents);
        (void)fclose(file);
        (void)fputs("doorfast: legacy configuration is unreadable or too large\n", stderr);
        return 2;
    }
    (void)fclose(file);

    if (df_config_import_legacy(contents, &imported) != DF_OK) {
        free(contents);
        (void)fputs("doorfast: legacy configuration is not supported\n", stderr);
        return 2;
    }
    free(contents);

    (void)printf("config settings 'main'\n"
                 "\toption enabled '%d'\n"
                 "\toption brand '%s'\n"
                 "\toption mode 'transparent'\n"
                 "\toption capture_auto '1'\n"
                 "\toption capture_promiscuous '0'\n"
                 "\toption unlock '-1'\n"
                 "\toption hangup '-1'\n"
                 "\toption call_elev '0'\n",
                 imported.config.enabled ? 1 : 0,
                 imported.brand);
    return 0;
}

int main(int argc, char **argv) {
#ifdef DF_ALLOW_TEST_ROOT
    if (argc == 5 && strcmp(argv[1], "--preflight") == 0 &&
        strcmp(argv[3], "--root") == 0)
        return df_run_preflight(argv[2], argv[4]);
#endif
    if (argc == 4 && (strcmp(argv[1], "--inspect-pcap") == 0 ||
                     strcmp(argv[1], "--simulate-handshake-pcap") == 0)) {
        bool simulate = strcmp(argv[1], "--simulate-handshake-pcap") == 0;
        uint8_t identity[6];
        struct df_gvs_session session = {0};
        struct df_gvs_replay_stats stats = {0};
        if (df_gvs_identity_parse(argv[3], identity) != DF_OK) {
            (void)fputs("doorfast: invalid indoor address\n", stderr);
            return 2;
        }
        if ((simulate ? df_gvs_replay_handshake_file(argv[2], identity, &session, &stats)
                      : df_gvs_replay_file(argv[2], identity, &session, &stats)) != DF_OK) {
            (void)fputs("doorfast: cannot inspect capture\n", stderr);
            return 2;
        }
        (void)printf("packets=%zu control=%zu accepted=%zu invalid=%zu state=%d talking=%zu pick_rejected=%zu calls=%zu ended=%zu hangup=%zu preempt=%zu timeout=%zu bad_time=%zu sync=%zu sync_rejected=%zu\n",
                     stats.packets_seen, stats.control_datagrams, stats.accepted_calls,
                     stats.invalid_datagrams, (int)session.state,
                     stats.talking_transitions, stats.rejected_pick_frames,
                     stats.calls_started, stats.ended_transitions,
                     stats.observed_hangups, stats.preempted_sessions,
                     stats.timed_out_sessions, stats.invalid_timestamps,
                     stats.time_sync_updates, stats.rejected_time_sync);
        if (simulate)
            (void)printf("mode=simulated transport=memory frames=%zu disconnects=%zu received=%zu\n",
                stats.simulated_frames, stats.simulated_disconnects, stats.handshake_received);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        df_print_usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--import-legacy") == 0) {
        return df_import_legacy_file(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--config") == 0) {
        return df_run_config(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--preflight") == 0) {
        return df_run_preflight(argv[2], "/");
    }
    df_print_usage(stderr);
    return 2;
}
#endif
