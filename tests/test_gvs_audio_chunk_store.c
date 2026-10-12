#include "test.h"
#include "gvs_audio_chunk_store.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int count_wav_files(const char *directory)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    int count = 0;

    if (dir == NULL) return -1;
    while ((entry = readdir(dir)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length > 4U && strcmp(entry->d_name + length - 4U, ".wav") == 0)
            count++;
    }
    (void)closedir(dir);
    return count;
}

static void write_wav(const char *source, size_t bytes)
{
    unsigned char content[3246] = {
        'R', 'I', 'F', 'F', 0x64, 0x06, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x40, 0x1f, 0, 0, 0x80, 0x3e, 0, 0, 2, 0, 16, 0,
        'd', 'a', 't', 'a', 0x40, 0x06, 0, 0
    };
    FILE *file = fopen(source, "wb");

    if (bytes >= 44U) {
        uint32_t riff_bytes = (uint32_t)(bytes - 8U);
        uint32_t pcm_bytes = (uint32_t)(bytes - 44U);
        for (unsigned i = 0; i < 4U; i++) {
            content[4U + i] = (unsigned char)(riff_bytes >> (8U * i));
            content[40U + i] = (unsigned char)(pcm_bytes >> (8U * i));
        }
    }
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file == NULL) return;
    TEST_ASSERT_INT_EQ((int)bytes, (int)fwrite(content, 1, bytes, file));
    TEST_ASSERT_INT_EQ(0, fclose(file));
}

static void test_chunk_store_v1(void)
{
    char root[] = "/tmp/doorfast-audio-v1-XXXXXX";
    char directory[128];
    char source[128];
    char path[256];
    unsigned char original[1644] = {0};
    unsigned char published[1644] = {0};
    struct stat state = {0};
    FILE *file;
    unsigned revision;
    const size_t invalid_bytes[] = {0, 43, 45, 3246};

    TEST_ASSERT_INT_EQ(1, mkdtemp(root) != NULL);
    (void)snprintf(directory, sizeof(directory), "%s/store", root);
    (void)snprintf(source, sizeof(source), "%s/source.wav", root);
    write_wav(source, 1644);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish(
        directory, source, 7, 29, 30, 1644, 3));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 30, 40, 1644, 0, 1000, false));
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-30-40-1644-0-1000-0.wav", directory);
    TEST_ASSERT_INT_EQ(0, stat(path, &state));
    TEST_ASSERT_INT_EQ(1644, (int)state.st_size);
    TEST_ASSERT_INT_EQ(0, (int)(state.st_mode & 0077));
    file = fopen(source, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) {
        TEST_ASSERT_INT_EQ(1644, (int)fread(original, 1, 1644, file));
        TEST_ASSERT_INT_EQ(0, fclose(file));
    }
    file = fopen(path, "rb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) {
        TEST_ASSERT_INT_EQ(1644, (int)fread(published, 1, 1644, file));
        TEST_ASSERT_INT_EQ(0, fclose(file));
        TEST_ASSERT_INT_EQ(0, memcmp(original, published, 1644));
    }
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 30, 40, 1644, 0, 1000, false));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 31, 40, 1644, 17, 2000, true));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        directory, source, 7, 31, 40, 1644, 17));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 20, 30, 1644, 0, 2000, true));
    TEST_ASSERT_INT_EQ(2, count_wav_files(directory));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 40, 41, 1644, 160, UINT64_MAX, true));
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-40-41-1644-160-18446744073709551615-1.wav",
        directory);
    TEST_ASSERT_INT_EQ(0, access(path, R_OK));
    for (revision = 1; revision <= 18; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
            directory, source, 8, revision - 1U, revision, 1644,
            0, revision * 100U, false));
    }
    TEST_ASSERT_INT_EQ(20, count_wav_files(directory));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-29-30-1644-3.wav",
        directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-30-40-1644-0-1000-0.wav", directory);
    TEST_ASSERT_INT_EQ(0, access(path, F_OK));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_clear(directory));

    for (size_t i = 0; i < sizeof(invalid_bytes) / sizeof(invalid_bytes[0]); i++) {
        write_wav(source, invalid_bytes[i]);
        TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
            directory, source, 7, 0, 1, invalid_bytes[i], 0, 0, false));
        TEST_ASSERT_INT_EQ(-1, access(directory, F_OK));
    }
    write_wav(source, 44);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 0, 1, 44, 0, 0, false));
    write_wav(source, 46);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 1, 2, 46, 1, 0, true));
    write_wav(source, 3244);
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, UINT64_MAX, UINT64_MAX - 1U, UINT64_MAX,
        3244, UINT64_MAX, UINT64_MAX, true));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 0, 0, 1, 3244, 0, 0, false));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 1, 1, 3244, 0, 0, false));
    write_wav(source, 1644);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 2, 3, 3244, 0, 0, false));
    TEST_ASSERT_INT_EQ(3, count_wav_files(directory));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(0, unlink(source));
    TEST_ASSERT_INT_EQ(0, rmdir(root));
}

static void test_chunk_store_v1_prune_failure(void)
{
    char root[] = "/tmp/doorfast-audio-v1-failure-XXXXXX";
    char directory[128];
    char source[128];
    char path[256];
    unsigned revision;

    TEST_ASSERT_INT_EQ(1, mkdtemp(root) != NULL);
    (void)snprintf(directory, sizeof(directory), "%s/store", root);
    (void)snprintf(source, sizeof(source), "%s/source.wav", root);
    write_wav(source, 1644);
    for (revision = 2; revision <= DF_GVS_AUDIO_CHUNK_LIMIT; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
            directory, source, 7, revision - 1U, revision, 1644, 0,
            revision * 100U, false));
    }
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-0-1-1644-0-100-0.wav", directory);
    TEST_ASSERT_INT_EQ(0, mkdir(path, 0700));
    TEST_ASSERT_INT_EQ(20, count_wav_files(directory));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 20, 21, 1644, 0, 2100, false));
    TEST_ASSERT_INT_EQ(20, count_wav_files(directory));
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-20-21-1644-0-2100-0.wav", directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    for (revision = 2; revision <= 20; revision++) {
        (void)snprintf(path, sizeof(path),
            "%s/chunk-7-%u-%u-1644-0-%u-0.wav", directory,
            revision - 1U, revision, revision * 100U);
        TEST_ASSERT_INT_EQ(0, access(path, F_OK));
    }
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-0-1-1644-0-100-0.wav", directory);
    TEST_ASSERT_INT_EQ(0, rmdir(path));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(0, unlink(source));
    TEST_ASSERT_INT_EQ(0, rmdir(root));
}

static void test_chunk_store_clear_scope(void)
{
    char root[] = "/tmp/doorfast-audio-scope-XXXXXX";
    char directory[128];
    char source[128];
    char link[128];
    char path[256];
    const char *invalid_names[] = {
        "keep.wav", "chunk-victim.wav", ".chunk-note",
        "chunk-7-0-1-1644-0-1000-2.wav",
        "chunk-7-0-1-1644-0-18446744073709551616-0.wav",
        "chunk-7-0-1-1644-0--1-0.wav",
        "chunk-7-0-1-1644-0-+1-0.wav",
        "chunk-7-0-1-1645-0-1000-0.wav",
        "chunk-7-0-1-3246-0-1000-0.wav",
        "chunk-7-0-1-1644-0-1000-0.wav.extra",
        "chunk-0-0-1-4-0.wav", "chunk-7-1-1-4-0.wav"
    };

    TEST_ASSERT_INT_EQ(1, mkdtemp(root) != NULL);
    (void)snprintf(directory, sizeof(directory), "%s/store", root);
    (void)snprintf(source, sizeof(source), "%s/source.wav", root);
    (void)snprintf(link, sizeof(link), "%s/link", root);
    write_wav(source, 1644);
    TEST_ASSERT_INT_EQ(0, mkdir(directory, 0700));
    TEST_ASSERT_INT_EQ(0, symlink(directory, link));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        link, source, 7, 0, 1, 1644, 0, 1000, false));
    TEST_ASSERT_INT_EQ(0, count_wav_files(directory));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_clear(link));
    TEST_ASSERT_INT_EQ(0, access(directory, F_OK));
    TEST_ASSERT_INT_EQ(0, unlink(link));
    TEST_ASSERT_INT_EQ(0, chmod(directory, 0755));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 0, 1, 1644, 0, 1000, false));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(0, chmod(directory, 0700));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
        directory, source, 7, 0, 1, 1644, 0, 1000, false));
    for (size_t i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]); i++) {
        (void)snprintf(path, sizeof(path), "%s/%s", directory, invalid_names[i]);
        write_wav(path, 1644);
    }
    for (unsigned revision = 2; revision <= 21; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish_v1(
            directory, source, 7, revision - 1U, revision, 1644, 0,
            revision * 1000U, false));
    }
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-0-1-1644-0-1000-0.wav", directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    for (unsigned revision = 2; revision <= 21; revision++) {
        (void)snprintf(path, sizeof(path),
            "%s/chunk-7-%u-%u-1644-0-%u-0.wav", directory,
            revision - 1U, revision, revision * 1000U);
        TEST_ASSERT_INT_EQ(0, access(path, R_OK));
    }
    (void)snprintf(path, sizeof(path), "%s/.chunk-Ab12xy", directory);
    write_wav(path, 4);
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(0, access(source, R_OK));
    (void)snprintf(path, sizeof(path), "%s/.chunk-Ab12xy", directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    (void)snprintf(path, sizeof(path),
        "%s/chunk-7-0-1-1644-0-1000-0.wav", directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    for (unsigned revision = 2; revision <= 21; revision++) {
        (void)snprintf(path, sizeof(path),
            "%s/chunk-7-%u-%u-1644-0-%u-0.wav", directory,
            revision - 1U, revision, revision * 1000U);
        TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    }
    for (size_t i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]); i++) {
        (void)snprintf(path, sizeof(path), "%s/%s", directory, invalid_names[i]);
        TEST_ASSERT_INT_EQ(0, access(path, R_OK));
        (void)unlink(path);
    }
    TEST_ASSERT_INT_EQ(0, rmdir(directory));
    TEST_ASSERT_INT_EQ(0, unlink(source));
    TEST_ASSERT_INT_EQ(0, rmdir(root));
}

void test_gvs_audio_chunk_store(void)
{
    char root[] = "/tmp/doorfast-audio-chunks-XXXXXX";
    char directory[128];
    char source[160];
    char path[256];
    char failure_directory[160];
    char symlink_directory[160];
    char symlink_target[160];
    const unsigned char content[] = {'R', 'I', 'F', 'F'};
    FILE *file;
    unsigned revision;

    TEST_ASSERT_INT_EQ(1, mkdtemp(root) != NULL);
    (void)snprintf(directory, sizeof(directory), "%s/store", root);
    (void)snprintf(source, sizeof(source), "%s/source.wav", root);
    file = fopen(source, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file == NULL) return;
    TEST_ASSERT_INT_EQ(4, (int)fwrite(content, 1, sizeof(content), file));
    TEST_ASSERT_INT_EQ(0, fclose(file));

    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        directory, source, 0, 0, 1, sizeof(content), 0));
    for (revision = 1; revision <= 21; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish(
            directory, source, 7, revision - 1U, revision,
            sizeof(content), 0));
    }
    TEST_ASSERT_INT_EQ(20, count_wav_files(directory));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-0-1-4-0.wav",
                   directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    for (revision = 2; revision <= 21; revision++) {
        (void)snprintf(path, sizeof(path), "%s/chunk-7-%u-%u-4-0.wav",
                       directory, revision - 1U, revision);
        TEST_ASSERT_INT_EQ(0, access(path, R_OK));
    }
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(-1, access(directory, F_OK));

    (void)snprintf(failure_directory, sizeof(failure_directory),
                   "%s-failure", directory);
    TEST_ASSERT_INT_EQ(0, mkdir(failure_directory, 0700));
    for (revision = 2; revision <= DF_GVS_AUDIO_CHUNK_LIMIT; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish(
            failure_directory, source, 7, revision - 1U, revision,
            sizeof(content), 0));
    }
    (void)snprintf(path, sizeof(path), "%s/chunk-7-0-1-4-0.wav",
                   failure_directory);
    TEST_ASSERT_INT_EQ(0, mkdir(path, 0700));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        failure_directory, source, 7, DF_GVS_AUDIO_CHUNK_LIMIT,
        DF_GVS_AUDIO_CHUNK_LIMIT + 1U, sizeof(content), 0));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-%u-%u-4-0.wav",
                   failure_directory, DF_GVS_AUDIO_CHUNK_LIMIT,
                   DF_GVS_AUDIO_CHUNK_LIMIT + 1U);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-0-1-4-0.wav",
                   failure_directory);
    TEST_ASSERT_INT_EQ(0, rmdir(path));
    TEST_ASSERT_INT_EQ(0,
        df_gvs_audio_chunk_store_clear(failure_directory));

    (void)snprintf(symlink_directory, sizeof(symlink_directory),
                   "%s-link", directory);
    (void)snprintf(symlink_target, sizeof(symlink_target),
                   "%s-target", directory);
    TEST_ASSERT_INT_EQ(0, mkdir(symlink_target, 0700));
    TEST_ASSERT_INT_EQ(0, symlink(symlink_target, symlink_directory));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        symlink_directory, source, 7, 0, 1, sizeof(content), 0));
    (void)snprintf(path, sizeof(path), "%s/chunk-victim.wav",
                   symlink_target);
    file = fopen(path, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file != NULL) TEST_ASSERT_INT_EQ(0, fclose(file));
    TEST_ASSERT_INT_EQ(-1,
        df_gvs_audio_chunk_store_clear(symlink_directory));
    TEST_ASSERT_INT_EQ(0, access(path, F_OK));
    TEST_ASSERT_INT_EQ(0, unlink(path));
    TEST_ASSERT_INT_EQ(0, unlink(symlink_directory));
    TEST_ASSERT_INT_EQ(0, rmdir(symlink_target));
    (void)unlink(source);
    TEST_ASSERT_INT_EQ(0, rmdir(root));
    test_chunk_store_v1();
    test_chunk_store_v1_prune_failure();
    test_chunk_store_clear_scope();
}
