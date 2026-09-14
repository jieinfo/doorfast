#include "test.h"
#include "gvs_audio_chunk_store.h"

#include <dirent.h>
#include <stdio.h>
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

void test_gvs_audio_chunk_store(void)
{
    char directory[128];
    char source[160];
    char path[256];
    char failure_directory[160];
    char symlink_directory[160];
    char symlink_target[160];
    const unsigned char content[] = {'R', 'I', 'F', 'F'};
    FILE *file;
    unsigned revision;

    (void)snprintf(directory, sizeof(directory),
                   "/tmp/doorfast-audio-chunks-%ld", (long)getpid());
    (void)snprintf(source, sizeof(source), "%s-source.wav", directory);
    file = fopen(source, "wb");
    TEST_ASSERT_INT_EQ(1, file != NULL);
    if (file == NULL) return;
    TEST_ASSERT_INT_EQ(4, (int)fwrite(content, 1, sizeof(content), file));
    TEST_ASSERT_INT_EQ(0, fclose(file));

    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        directory, source, 0, 0, 1, sizeof(content), 0));
    for (revision = 1; revision <= 5; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish(
            directory, source, 7, revision - 1U, revision,
            sizeof(content), 0));
    }
    TEST_ASSERT_INT_EQ(DF_GVS_AUDIO_CHUNK_LIMIT,
                       count_wav_files(directory));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-0-1-4-0.wav",
                   directory);
    TEST_ASSERT_INT_EQ(-1, access(path, F_OK));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-1-2-4-0.wav",
                   directory);
    TEST_ASSERT_INT_EQ(0, access(path, R_OK));
    TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_clear(directory));
    TEST_ASSERT_INT_EQ(-1, access(directory, F_OK));

    (void)snprintf(failure_directory, sizeof(failure_directory),
                   "%s-failure", directory);
    TEST_ASSERT_INT_EQ(0, mkdir(failure_directory, 0700));
    for (revision = 2; revision <= 4; revision++) {
        TEST_ASSERT_INT_EQ(0, df_gvs_audio_chunk_store_publish(
            failure_directory, source, 7, revision - 1U, revision,
            sizeof(content), 0));
    }
    (void)snprintf(path, sizeof(path), "%s/chunk-7-0-1-4-0.wav",
                   failure_directory);
    TEST_ASSERT_INT_EQ(0, mkdir(path, 0700));
    TEST_ASSERT_INT_EQ(-1, df_gvs_audio_chunk_store_publish(
        failure_directory, source, 7, 4, 5, sizeof(content), 0));
    (void)snprintf(path, sizeof(path), "%s/chunk-7-4-5-4-0.wav",
                   failure_directory);
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
}
