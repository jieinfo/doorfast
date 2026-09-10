#include "evidence_log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

enum { DF_EVIDENCE_FIELD_MAX = 80, DF_EVIDENCE_LINE_MAX = 2048 };

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
        length >= DF_EVIDENCE_LOG_PREFIX_MAX || !strcmp(prefix, ".") ||
        !strcmp(prefix, "..")) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!isalnum((unsigned char)prefix[i]) && prefix[i] != '-' &&
            prefix[i] != '_') return 0;
    return 1;
}

static int df_slot_path(const struct df_evidence_log *log, uint32_t slot,
                        const char *suffix, char *output, size_t size) {
    int written = snprintf(output, size, "%s/%s-%03u.%s", log->directory,
                           log->prefix, slot, suffix);
    return written >= 0 && (size_t)written < size ? DF_OK : DF_ERR_INVALID;
}

static int df_open_partial(struct df_evidence_log *log) {
    char path[DF_EVIDENCE_LOG_DIRECTORY_MAX + DF_EVIDENCE_LOG_PREFIX_MAX + 32];
    char completed[sizeof(path)];
    struct stat status;
    int descriptor;

    if (df_slot_path(log, log->active_slot, "partial", path, sizeof(path)) != DF_OK ||
        df_slot_path(log, log->active_slot, "jsonl", completed,
                     sizeof(completed)) != DF_OK) return DF_ERR_INVALID;
    log->replaced_bytes = 0;
    if (lstat(completed, &status) == 0) {
        if (!S_ISREG(status.st_mode) || status.st_size < 0) return DF_ERR_INVALID;
        log->replaced_bytes = (uint64_t)status.st_size;
    } else if (errno != ENOENT) return DF_ERR_IO;
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (descriptor < 0) return DF_ERR_IO;
    log->file = fdopen(descriptor, "wb");
    if (log->file == NULL) {
        (void)close(descriptor);
        (void)unlink(path);
        return DF_ERR_IO;
    }
    log->current_bytes = 0;
    log->has_records = false;
    return DF_OK;
}

static int df_finalize_partial(struct df_evidence_log *log) {
    char partial[DF_EVIDENCE_LOG_DIRECTORY_MAX + DF_EVIDENCE_LOG_PREFIX_MAX + 32];
    char completed[sizeof(partial)];
    int failed = 0;

    if (log->file == NULL ||
        df_slot_path(log, log->active_slot, "partial", partial,
                     sizeof(partial)) != DF_OK ||
        df_slot_path(log, log->active_slot, "jsonl", completed,
                     sizeof(completed)) != DF_OK) return DF_ERR_INVALID;
    if (fflush(log->file) != 0) failed = 1;
    if (fsync(fileno(log->file)) != 0) failed = 1;
    if (fclose(log->file) != 0) failed = 1;
    log->file = NULL;
    if (failed) return DF_ERR_IO;
    if (!log->has_records) return unlink(partial) == 0 ? DF_OK : DF_ERR_IO;
    if (rename(partial, completed) != 0) return DF_ERR_IO;
    if (log->replaced_bytes > 0) {
        log->stored_bytes -= log->replaced_bytes;
    } else {
        log->completed_segments++;
    }
    log->replaced_bytes = 0;
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

static int df_append_text(char *line, size_t size, size_t *used,
                          const char *format, ...) {
    va_list arguments;
    int written;
    if (*used >= size) return DF_ERR_INVALID;
    va_start(arguments, format);
    written = vsnprintf(line + *used, size - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= size - *used) return DF_ERR_INVALID;
    *used += (size_t)written;
    return DF_OK;
}

static int df_append_json_string(char *line, size_t size, size_t *used,
                                 const char *value) {
    size_t length;
    if (value == NULL || (length = strnlen(value, DF_EVIDENCE_FIELD_MAX + 1)) >
        DF_EVIDENCE_FIELD_MAX) return DF_ERR_INVALID;
    if (df_append_text(line, size, used, "\"") != DF_OK) return DF_ERR_INVALID;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)value[i];
        if (byte == '"' || byte == '\\') {
            if (df_append_text(line, size, used, "\\%c", byte) != DF_OK)
                return DF_ERR_INVALID;
        } else if (byte < 0x20) {
            if (df_append_text(line, size, used, "\\u%04x", byte) != DF_OK)
                return DF_ERR_INVALID;
        } else if (df_append_text(line, size, used, "%c", byte) != DF_OK) {
            return DF_ERR_INVALID;
        }
    }
    return df_append_text(line, size, used, "\"");
}

static int df_build_line(const struct df_evidence_log_record *record,
                         char *line, size_t size, size_t *length) {
#define TEXT(value) do { if (df_append_json_string(line, size, length, (value)) != DF_OK) return DF_ERR_INVALID; } while (0)
    *length = 0;
    if (record == NULL || record->wall_microseconds > 999999) return DF_ERR_INVALID;
    if (df_append_text(line, size, length,
        "{\"wall_seconds\":%" PRIu64 ",\"wall_microseconds\":%u,"
        "\"monotonic_ms\":%" PRIu64 ",\"interface\":",
        record->wall_seconds, record->wall_microseconds,
        record->monotonic_ms) != DF_OK) return DF_ERR_INVALID;
    TEXT(record->interface_name);
    if (df_append_text(line, size, length, ",\"source_mac\":") != DF_OK) return DF_ERR_INVALID;
    TEXT(record->source_mac);
    if (df_append_text(line, size, length, ",\"destination_mac\":") != DF_OK) return DF_ERR_INVALID;
    TEXT(record->destination_mac);
    if (df_append_text(line, size, length, ",\"source_ip\":") != DF_OK) return DF_ERR_INVALID;
    TEXT(record->source_ip);
    if (df_append_text(line, size, length, ",\"destination_ip\":") != DF_OK) return DF_ERR_INVALID;
    TEXT(record->destination_ip);
    if (df_append_text(line, size, length,
        ",\"source_port\":%u,\"destination_port\":%u,\"source_gvs\":",
        record->source_port, record->destination_port) != DF_OK) return DF_ERR_INVALID;
    TEXT(record->source_gvs);
    if (df_append_text(line, size, length, ",\"destination_gvs\":") != DF_OK) return DF_ERR_INVALID;
    TEXT(record->destination_gvs);
    if (df_append_text(line, size, length,
        ",\"family\":%u,\"opcode\":%u,\"length\":%u,"
        "\"session_generation\":%" PRIu64 ",\"health_state\":",
        record->family, record->opcode, record->length,
        record->session_generation) != DF_OK) return DF_ERR_INVALID;
    TEXT(record->health_state);
    if (df_append_text(line, size, length, "}\n") != DF_OK) return DF_ERR_INVALID;
#undef TEXT
    return DF_OK;
}

int df_evidence_log_open(struct df_evidence_log *out,
                         const struct df_evidence_log_config *config) {
    struct df_evidence_log log = {0};
    struct stat directory_status, oldest_status = {0};
    bool have_oldest = false, have_missing = false;

    if (out == NULL || config == NULL || config->directory == NULL ||
        config->directory[0] == '\0' ||
        strlen(config->directory) >= sizeof(log.directory) ||
        df_has_parent_component(config->directory) ||
        !df_prefix_valid(config->prefix) || config->segment_count < 2 ||
        config->segment_count > 1000 || config->segment_bytes == 0 ||
        config->segment_count > UINT64_MAX / config->segment_bytes ||
        lstat(config->directory, &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode)) return DF_ERR_INVALID;
    memcpy(log.directory, config->directory, strlen(config->directory) + 1);
    memcpy(log.prefix, config->prefix, strlen(config->prefix) + 1);
    log.segment_count = config->segment_count;
    log.segment_bytes = config->segment_bytes;

    for (uint32_t slot = 0; slot < log.segment_count; ++slot) {
        char path[DF_EVIDENCE_LOG_DIRECTORY_MAX + DF_EVIDENCE_LOG_PREFIX_MAX + 32];
        struct stat status;
        if (df_slot_path(&log, slot, "partial", path, sizeof(path)) != DF_OK)
            return DF_ERR_INVALID;
        if (lstat(path, &status) == 0) {
            if (!S_ISREG(status.st_mode) || unlink(path) != 0) return DF_ERR_INVALID;
        } else if (errno != ENOENT) return DF_ERR_IO;
        if (df_slot_path(&log, slot, "jsonl", path, sizeof(path)) != DF_OK)
            return DF_ERR_INVALID;
        if (lstat(path, &status) == 0) {
            if (!S_ISREG(status.st_mode) || status.st_size < 0) return DF_ERR_INVALID;
            log.completed_segments++;
            log.stored_bytes += (uint64_t)status.st_size;
            if (!have_missing && (!have_oldest || df_time_older(&status, &oldest_status))) {
                log.active_slot = slot;
                oldest_status = status;
                have_oldest = true;
            }
        } else if (errno == ENOENT) {
            if (!have_missing) log.active_slot = slot;
            have_missing = true;
        } else return DF_ERR_IO;
    }
    log.initialized = true;
    if (df_open_partial(&log) != DF_OK) return DF_ERR_IO;
    *out = log;
    return DF_OK;
}

int df_evidence_log_append(struct df_evidence_log *log,
                           const struct df_evidence_log_record *record) {
    char line[DF_EVIDENCE_LINE_MAX];
    size_t length;
    if (log == NULL || !log->initialized || log->file == NULL ||
        df_build_line(record, line, sizeof(line), &length) != DF_OK ||
        length > log->segment_bytes) return DF_ERR_INVALID;
    if (log->has_records && log->current_bytes + length > log->segment_bytes) {
        if (df_finalize_partial(log) != DF_OK) return DF_ERR_IO;
        log->active_slot = (log->active_slot + 1) % log->segment_count;
        if (df_open_partial(log) != DF_OK) return DF_ERR_IO;
    }
    if (fwrite(line, 1, length, log->file) != length || fflush(log->file) != 0)
        return DF_ERR_IO;
    log->current_bytes += length;
    log->stored_bytes += length;
    log->has_records = true;
    return DF_OK;
}

int df_evidence_log_status(const struct df_evidence_log *log,
                           struct df_evidence_log_status *status) {
    if (log == NULL || !log->initialized || status == NULL) return DF_ERR_INVALID;
    *status = (struct df_evidence_log_status){
        .active_slot = log->active_slot,
        .completed_segments = log->completed_segments,
        .bytes_written = log->stored_bytes,
    };
    return DF_OK;
}

int df_evidence_log_close(struct df_evidence_log *log) {
    int result;
    if (log == NULL || !log->initialized || log->file == NULL)
        return DF_ERR_INVALID;
    result = df_finalize_partial(log);
    memset(log, 0, sizeof(*log));
    return result;
}
