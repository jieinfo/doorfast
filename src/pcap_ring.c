#include "pcap_ring.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

static void df_le16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void df_le32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static int df_has_parent_component(const char *path) {
    const char *cursor = path;
    while ((cursor = strstr(cursor, "..")) != NULL) {
        if ((cursor == path || cursor[-1] == '/') &&
            (cursor[2] == '\0' || cursor[2] == '/')) return 1;
        cursor += 2;
    }
    return 0;
}

static int df_prefix_valid(const char *prefix) {
    size_t length;
    if (prefix == NULL || (length = strlen(prefix)) == 0 ||
        length >= DF_PCAP_RING_PREFIX_MAX || !strcmp(prefix, ".") ||
        !strcmp(prefix, "..")) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!isalnum((unsigned char)prefix[i]) && prefix[i] != '-' &&
            prefix[i] != '_') return 0;
    return 1;
}

static int df_slot_path(const struct df_pcap_ring *ring, uint32_t slot,
                        const char *suffix, char *output, size_t size) {
    int written = snprintf(output, size, "%s/%s-%03u.%s", ring->directory,
                           ring->prefix, slot, suffix);
    return written >= 0 && (size_t)written < size ? DF_OK : DF_ERR_INVALID;
}

static int df_write_all(FILE *file, const uint8_t *bytes, size_t length) {
    return fwrite(bytes, 1, length, file) == length ? DF_OK : DF_ERR_IO;
}

static int df_open_partial(struct df_pcap_ring *ring) {
    uint8_t header[24] = {0};
    char path[DF_PCAP_RING_DIRECTORY_MAX + DF_PCAP_RING_PREFIX_MAX + 32];
    char completed[sizeof(path)];
    struct stat status;
    int descriptor;

    if (df_slot_path(ring, ring->active_slot, "partial", path, sizeof(path)) != DF_OK ||
        df_slot_path(ring, ring->active_slot, "pcap", completed,
                     sizeof(completed)) != DF_OK) return DF_ERR_INVALID;
    ring->replaced_bytes = 0;
    if (lstat(completed, &status) == 0) {
        if (!S_ISREG(status.st_mode) || status.st_size < 0) return DF_ERR_INVALID;
        ring->replaced_bytes = (uint64_t)status.st_size;
    } else if (errno != ENOENT) return DF_ERR_IO;
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (descriptor < 0) return DF_ERR_IO;
    ring->file = fdopen(descriptor, "wb");
    if (ring->file == NULL) {
        (void)close(descriptor);
        (void)unlink(path);
        return DF_ERR_IO;
    }
    df_le32(header, UINT32_C(0xa1b2c3d4));
    df_le16(header + 4, 2);
    df_le16(header + 6, 4);
    df_le32(header + 16, ring->snaplen);
    df_le32(header + 20, 1);
    if (df_write_all(ring->file, header, sizeof(header)) != DF_OK ||
        fflush(ring->file) != 0) {
        (void)fclose(ring->file);
        ring->file = NULL;
        (void)unlink(path);
        return DF_ERR_IO;
    }
    ring->current_bytes = sizeof(header);
    ring->stored_bytes += sizeof(header);
    ring->has_records = false;
    return DF_OK;
}

static int df_finalize_partial(struct df_pcap_ring *ring) {
    char partial[DF_PCAP_RING_DIRECTORY_MAX + DF_PCAP_RING_PREFIX_MAX + 32];
    char completed[sizeof(partial)];

    if (ring->file == NULL ||
        df_slot_path(ring, ring->active_slot, "partial", partial,
                     sizeof(partial)) != DF_OK ||
        df_slot_path(ring, ring->active_slot, "pcap", completed,
                     sizeof(completed)) != DF_OK) return DF_ERR_INVALID;
    int failed = 0;
    if (fflush(ring->file) != 0) failed = 1;
    if (fsync(fileno(ring->file)) != 0) failed = 1;
    if (fclose(ring->file) != 0) failed = 1;
    ring->file = NULL;
    if (failed) return DF_ERR_IO;
    if (!ring->has_records) {
        ring->stored_bytes -= ring->current_bytes;
        return unlink(partial) == 0 ? DF_OK : DF_ERR_IO;
    }
    if (rename(partial, completed) != 0) return DF_ERR_IO;
    if (ring->replaced_bytes > 0) {
        ring->stored_bytes -= ring->replaced_bytes;
    } else {
        ring->completed_segments++;
    }
    ring->replaced_bytes = 0;
    return DF_OK;
}

static int df_time_older(const struct stat *left, const struct stat *right) {
#ifdef __APPLE__
    if (left->st_mtimespec.tv_sec != right->st_mtimespec.tv_sec)
        return left->st_mtimespec.tv_sec < right->st_mtimespec.tv_sec;
    return left->st_mtimespec.tv_nsec < right->st_mtimespec.tv_nsec;
#else
    if (left->st_mtim.tv_sec != right->st_mtim.tv_sec)
        return left->st_mtim.tv_sec < right->st_mtim.tv_sec;
    return left->st_mtim.tv_nsec < right->st_mtim.tv_nsec;
#endif
}

int df_pcap_ring_init(struct df_pcap_ring *out,
                      const struct df_pcap_ring_config *config) {
    struct df_pcap_ring ring = {0};
    struct stat directory_status, oldest_status = {0};
    bool have_oldest = false, have_missing = false;

    if (out == NULL || config == NULL || config->directory == NULL ||
        config->directory[0] == '\0' ||
        strlen(config->directory) >= sizeof(ring.directory) ||
        df_has_parent_component(config->directory) ||
        !df_prefix_valid(config->prefix) || config->segment_count < 2 ||
        config->segment_count > 1000 || config->snaplen == 0 ||
        config->segment_bytes <= 40 ||
        config->segment_count > UINT64_MAX / config->segment_bytes ||
        lstat(config->directory, &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode)) return DF_ERR_INVALID;
    memcpy(ring.directory, config->directory, strlen(config->directory) + 1);
    memcpy(ring.prefix, config->prefix, strlen(config->prefix) + 1);
    ring.segment_count = config->segment_count;
    ring.segment_bytes = config->segment_bytes;
    ring.snaplen = config->snaplen;

    for (uint32_t slot = 0; slot < ring.segment_count; ++slot) {
        char path[DF_PCAP_RING_DIRECTORY_MAX + DF_PCAP_RING_PREFIX_MAX + 32];
        struct stat status;
        if (df_slot_path(&ring, slot, "partial", path, sizeof(path)) != DF_OK)
            return DF_ERR_INVALID;
        if (lstat(path, &status) == 0) {
            if (!S_ISREG(status.st_mode) || unlink(path) != 0) return DF_ERR_INVALID;
        } else if (errno != ENOENT) return DF_ERR_IO;
        if (df_slot_path(&ring, slot, "pcap", path, sizeof(path)) != DF_OK)
            return DF_ERR_INVALID;
        if (lstat(path, &status) == 0) {
            if (!S_ISREG(status.st_mode) || status.st_size < 0) return DF_ERR_INVALID;
            ring.completed_segments++;
            ring.stored_bytes += (uint64_t)status.st_size;
            if (!have_missing && (!have_oldest || df_time_older(&status, &oldest_status))) {
                ring.active_slot = slot;
                oldest_status = status;
                have_oldest = true;
            }
        } else if (errno == ENOENT) {
            if (!have_missing) ring.active_slot = slot;
            have_missing = true;
        } else return DF_ERR_IO;
    }
    ring.initialized = true;
    if (df_open_partial(&ring) != DF_OK) return DF_ERR_IO;
    *out = ring;
    return DF_OK;
}

int df_pcap_ring_write(struct df_pcap_ring *ring,
                       const struct df_capture_record *record) {
    uint8_t header[16];
    size_t captured;
    uint64_t record_bytes;

    if (ring == NULL || !ring->initialized || ring->file == NULL ||
        record == NULL || record->data == NULL || record->captured_length == 0 ||
        record->captured_length > record->original_length ||
        record->wall_seconds > UINT32_MAX || record->wall_microseconds > 999999)
        return DF_ERR_INVALID;
    captured = record->captured_length < ring->snaplen ?
        record->captured_length : ring->snaplen;
    record_bytes = 16 + captured;
    if (record_bytes > ring->segment_bytes - 24) return DF_ERR_INVALID;
    if (ring->has_records && ring->current_bytes + record_bytes > ring->segment_bytes) {
        if (df_finalize_partial(ring) != DF_OK) return DF_ERR_IO;
        ring->active_slot = (ring->active_slot + 1) % ring->segment_count;
        if (df_open_partial(ring) != DF_OK) return DF_ERR_IO;
    }
    df_le32(header, (uint32_t)record->wall_seconds);
    df_le32(header + 4, record->wall_microseconds);
    df_le32(header + 8, (uint32_t)captured);
    df_le32(header + 12, (uint32_t)record->original_length);
    if (df_write_all(ring->file, header, sizeof(header)) != DF_OK ||
        df_write_all(ring->file, record->data, captured) != DF_OK ||
        fflush(ring->file) != 0) return DF_ERR_IO;
    ring->current_bytes += record_bytes;
    ring->stored_bytes += record_bytes;
    ring->has_records = true;
    return DF_OK;
}

int df_pcap_ring_status(const struct df_pcap_ring *ring,
                        struct df_pcap_ring_status *status) {
    if (ring == NULL || !ring->initialized || status == NULL) return DF_ERR_INVALID;
    *status = (struct df_pcap_ring_status){
        .active_slot = ring->active_slot,
        .completed_segments = ring->completed_segments,
        .bytes_written = ring->stored_bytes,
    };
    return DF_OK;
}

int df_pcap_ring_close(struct df_pcap_ring *ring) {
    int result;
    if (ring == NULL || !ring->initialized || ring->file == NULL)
        return DF_ERR_INVALID;
    result = df_finalize_partial(ring);
    memset(ring, 0, sizeof(*ring));
    return result;
}
