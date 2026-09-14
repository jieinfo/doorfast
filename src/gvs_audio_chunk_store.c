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
    unsigned long long parsed_generation;
    unsigned long long previous;
    unsigned long long parsed_revision;
    unsigned long long dropped;
    size_t bytes;
    char tail;

    if (name == NULL || generation == NULL || revision == NULL ||
        sscanf(name, "chunk-%llu-%llu-%llu-%zu-%llu.wav%c",
               &parsed_generation, &previous, &parsed_revision, &bytes,
               &dropped, &tail) != 5)
        return -1;
    *generation = (uint64_t)parsed_generation;
    *revision = (uint64_t)parsed_revision;
    return 0;
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
            if (generation < oldest_generation ||
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

int df_gvs_audio_chunk_store_publish(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped_bytes)
{
    char destination[512];
    char temporary[512];
    struct stat state;

    if (directory == NULL || source == NULL || generation == 0U ||
        revision <= previous_revision || bytes == 0U)
        return -1;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    if (lstat(directory, &state) != 0 || !S_ISDIR(state.st_mode) ||
        state.st_uid != geteuid() || (state.st_mode & 0077) != 0 ||
        stat(source, &state) != 0 || !S_ISREG(state.st_mode) ||
        (uint64_t)state.st_size != (uint64_t)bytes)
        return -1;
    {
        int destination_length = snprintf(destination, sizeof(destination),
            "%s/chunk-%llu-%llu-%llu-%zu-%llu.wav", directory,
            (unsigned long long)generation,
            (unsigned long long)previous_revision,
            (unsigned long long)revision, bytes,
            (unsigned long long)dropped_bytes);
        int temporary_length = snprintf(temporary, sizeof(temporary),
            "%s/.chunk-XXXXXX", directory);
        if (destination_length < 0 || temporary_length < 0 ||
            (size_t)destination_length >= sizeof(destination) ||
            (size_t)temporary_length >= sizeof(temporary))
            return -1;
    }
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
        if (strncmp(entry->d_name, "chunk-", 6) != 0 &&
            strncmp(entry->d_name, ".chunk-", 7) != 0)
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
