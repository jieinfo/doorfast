#include "recorder_selftest.h"
#include "evidence_recorder.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int space(void *context, uint64_t *bytes) {
    *bytes = *(uint64_t *)context;
    return DF_OK;
}
int df_recorder_selftest(const char *path) {
    struct stat st;
    struct df_pcap_ring recent = {0}, control = {0};
    struct df_evidence_recorder recorder;
    uint64_t available = 100000;
    uint8_t bytes[84] = {0};
    struct df_capture_record packet = {.data = bytes, .captured_length = 84,
        .original_length = 84, .wall_seconds = 1};
    int result = 1;
    if (!path || strcmp(path, "/tmp/doorfast-recorder-selftest") ||
        lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0777) != 0700) return 2;
    DIR *directory = opendir(path);
    if (!directory) return 2;
    struct dirent *entry;
    int empty = 1;
    while ((entry = readdir(directory)))
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) empty = 0;
    closedir(directory);
    if (!empty) return 2;
    struct df_pcap_ring_config config = {.directory = path, .prefix = "recent",
        .segment_count = 2, .segment_bytes = 4096, .snaplen = 256};
    if (df_pcap_ring_init(&recent, &config)) goto done;
    config.prefix = "control"; config.snaplen = 2048;
    if (df_pcap_ring_init(&control, &config) ||
        df_evidence_recorder_init(&recorder, &recent, &control, 6000,
            space, &available)) goto done;
    bytes[12] = 8; bytes[14] = 0x45; bytes[17] = 70; bytes[23] = 17;
    bytes[34] = 0x20; bytes[35] = 0x6c; bytes[39] = 50;
    memcpy(bytes + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    bytes[80] = 3; bytes[81] = 1;
    for (unsigned i = 0; i < 100; ++i)
        if (df_evidence_recorder_accept(&recorder, &packet)) goto done;
    if (recent.completed_segments != 2 || control.completed_segments != 2 ||
        recorder.control_packets != 100) goto done;
    available = 6000;
    if (df_evidence_recorder_accept(&recorder, &packet) ||
        recorder.state != DF_RECORDER_SPACE_GUARD ||
        recorder.control_packets != 100) goto done;
    if (df_pcap_ring_close(&control)) goto done;
    if (df_pcap_ring_init(&control, &config) || control.completed_segments != 2)
        goto done;
    result = 0;
done:
    if (recent.file && df_pcap_ring_close(&recent)) result = 1;
    if (control.file && df_pcap_ring_close(&control)) result = 1;
    const char *prefixes[] = {"recent", "control"};
    const char *suffixes[] = {"pcap", "partial"};
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned s = 0; s < 2; ++s)
            for (unsigned slot = 0; slot < 2; ++slot) {
                char name[256];
                snprintf(name, sizeof(name), "%s/%s-%03u.%s", path,
                    prefixes[p], slot, suffixes[s]);
                (void)unlink(name);
            }
    if (!result) puts("PASS: recorder rotation, reserve guard, reopen; no network capture");
    return result;
}
