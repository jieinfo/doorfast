#include "gvs_audio_chunk_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int df_audio_chunk_name(const char *name, uint64_t *generation,
                               uint64_t *revision)
{
    uint64_t fields[7];
    size_t count = 0;
    const char *cursor;

    if (name == NULL || generation == NULL || revision == NULL ||
        strncmp(name, "chunk-", 6) != 0)
        return -1;
    cursor = name + 6;
    for (;;) {
        uint64_t value = 0;
        if (count == 7U || *cursor < '0' || *cursor > '9') return -1;
        do {
            unsigned digit = (unsigned)(*cursor - '0');
            if (value > (UINT64_MAX - digit) / 10U) return -1;
            value = value * 10U + digit;
            cursor++;
        } while (*cursor >= '0' && *cursor <= '9');
        fields[count++] = value;
        if (*cursor != '-') break;
        cursor++;
    }
    if ((count != 5U && count != 7U) || strcmp(cursor, ".wav") != 0 ||
        fields[0] == 0U || fields[2] <= fields[1] ||
        fields[3] == 0U || fields[3] > SIZE_MAX)
        return -1;
    if (count == 7U && (fields[3] < 44U || fields[3] > 3244U ||
        (fields[3] - 44U) % 2U != 0U || fields[6] > 1U))
        return -1;
    *generation = fields[0];
    *revision = fields[2];
    return 0;
}

static int df_audio_chunk_revision_exists(const char *directory,
    uint64_t generation, uint64_t revision)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    int result = 0;

    if (dir == NULL) return -1;
    while ((entry = readdir(dir)) != NULL) {
        uint64_t parsed_generation;
        uint64_t parsed_revision;
        if (df_audio_chunk_name(entry->d_name, &parsed_generation,
                               &parsed_revision) == 0 &&
            generation == parsed_generation && revision == parsed_revision) {
            result = 1;
            break;
        }
    }
    (void)closedir(dir);
    return result;
}

static int df_audio_chunk_copy(const char *source, char *temporary,
                               size_t expected_bytes)
{
    FILE *input = fopen(source, "rb");
    FILE *output;
    int output_fd;
    unsigned char buffer[4096];
    size_t total = 0;
    size_t count;
    int failed = 0;

    if (input == NULL) return -1;
    output_fd = mkstemp(temporary);
    if (output_fd < 0) {
        (void)fclose(input);
        return -1;
    }
    output = fdopen(output_fd, "wb");
    if (output == NULL) {
        (void)close(output_fd);
        (void)fclose(input);
        (void)unlink(temporary);
        return -1;
    }
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            failed = 1;
            break;
        }
        total += count;
    }
    if (ferror(input) || total != expected_bytes || fflush(output) != 0)
        failed = 1;
    if (fclose(output) != 0) failed = 1;
    if (fclose(input) != 0) failed = 1;
    if (failed) {
        (void)unlink(temporary);
        return -1;
    }
    return 0;
}

static int df_audio_chunk_prune(const char *directory)
{
    for (;;) {
        DIR *dir = opendir(directory);
        struct dirent *entry;
        char oldest[256] = {0};
        uint64_t oldest_generation = UINT64_MAX;
        uint64_t oldest_revision = UINT64_MAX;
        int count = 0;

        if (dir == NULL) return -1;
        while ((entry = readdir(dir)) != NULL) {
            uint64_t generation;
            uint64_t revision;
            if (df_audio_chunk_name(entry->d_name, &generation,
                                    &revision) != 0)
                continue;
            count++;
            if (oldest[0] == '\0' || generation < oldest_generation ||
                (generation == oldest_generation &&
                 revision < oldest_revision)) {
                oldest_generation = generation;
                oldest_revision = revision;
                (void)snprintf(oldest, sizeof(oldest), "%s", entry->d_name);
            }
        }
        (void)closedir(dir);
        if (count <= DF_GVS_AUDIO_CHUNK_LIMIT) return 0;
        {
            char path[512];
            int length = snprintf(path, sizeof(path), "%s/%s", directory,
                                  oldest);
            if (oldest[0] == '\0' || length < 0 ||
                (size_t)length >= sizeof(path) ||
                unlink(path) != 0)
                return -1;
        }
    }
}

static int df_audio_chunk_publish(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped,
    bool v1, uint64_t timestamp_ms, bool discontinuity)
{
    char destination[512];
    char temporary[512];
    struct stat state;

    if (directory == NULL || source == NULL || generation == 0U ||
        revision <= previous_revision || bytes == 0U)
        return -1;
    if (v1 && (bytes < 44U || bytes > 3244U || (bytes - 44U) % 2U != 0U))
        return -1;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    if (lstat(directory, &state) != 0 || !S_ISDIR(state.st_mode) ||
        state.st_uid != geteuid() || (state.st_mode & 0077) != 0 ||
        stat(source, &state) != 0 || !S_ISREG(state.st_mode) ||
        (uint64_t)state.st_size != (uint64_t)bytes)
        return -1;
    {
        int destination_length;
        if (v1) {
            destination_length = snprintf(destination, sizeof(destination),
                "%s/chunk-%llu-%llu-%llu-%zu-%llu-%llu-%u.wav", directory,
                (unsigned long long)generation,
                (unsigned long long)previous_revision,
                (unsigned long long)revision, bytes,
                (unsigned long long)dropped,
                (unsigned long long)timestamp_ms, discontinuity ? 1U : 0U);
        } else {
            destination_length = snprintf(destination, sizeof(destination),
                "%s/chunk-%llu-%llu-%llu-%zu-%llu.wav", directory,
                (unsigned long long)generation,
                (unsigned long long)previous_revision,
                (unsigned long long)revision, bytes,
                (unsigned long long)dropped);
        }
        int temporary_length = snprintf(temporary, sizeof(temporary),
            "%s/.chunk-XXXXXX", directory);
        if (destination_length < 0 || temporary_length < 0 ||
            (size_t)destination_length >= sizeof(destination) ||
            (size_t)temporary_length >= sizeof(temporary))
            return -1;
    }
    if (df_audio_chunk_revision_exists(directory, generation, revision) != 0)
        return -1;
    if (lstat(destination, &state) == 0 || errno != ENOENT) return -1;
    if (df_audio_chunk_copy(source, temporary, bytes) != 0 ||
        rename(temporary, destination) != 0) {
        (void)unlink(temporary);
        return -1;
    }
    if (df_audio_chunk_prune(directory) != 0) {
        (void)unlink(destination);
        return -1;
    }
    return 0;
}

int df_gvs_audio_chunk_store_publish(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped_bytes)
{
    return df_audio_chunk_publish(directory, source, generation,
        previous_revision, revision, bytes, dropped_bytes, false, 0, false);
}

int df_gvs_audio_chunk_store_publish_v1(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped_samples,
    uint64_t timestamp_ms, bool discontinuity)
{
    return df_audio_chunk_publish(directory, source, generation,
        previous_revision, revision, bytes, dropped_samples, true,
        timestamp_ms, discontinuity);
}

static int df_audio_chunk_temporary_name(const char *name)
{
    if (strncmp(name, ".chunk-", 7) != 0 || strlen(name) != 13U)
        return 0;
    for (size_t i = 7; i < 13U; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9')))
            return 0;
    }
    return 1;
}

int df_gvs_audio_chunk_store_clear(const char *directory)
{
    DIR *dir;
    struct dirent *entry;
    struct stat state;
    int result = 0;

    if (directory == NULL) return -1;
    if (lstat(directory, &state) != 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(state.st_mode) || state.st_uid != geteuid() ||
        (state.st_mode & 0077) != 0)
        return -1;
    dir = opendir(directory);
    if (dir == NULL) return -1;
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        uint64_t generation;
        uint64_t revision;
        if (df_audio_chunk_name(entry->d_name, &generation, &revision) != 0 &&
            !df_audio_chunk_temporary_name(entry->d_name))
            continue;
        int length = snprintf(path, sizeof(path), "%s/%s", directory,
                              entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path) ||
            unlink(path) != 0)
            result = -1;
    }
    (void)closedir(dir);
    if (rmdir(directory) != 0 && errno != ENOENT) result = -1;
    return result;
}
