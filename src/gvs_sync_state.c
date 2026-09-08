#include "gvs_sync_state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DF_GVS_SYNC_STATE_MAX_BYTES 256U
#define DF_GVS_SYNC_STATE_PATH_MAX 256U

int df_gvs_sync_state_load(const char *path, uint16_t *version) {
    char contents[DF_GVS_SYNC_STATE_MAX_BYTES + 1U];
    unsigned parsed;
    char trailing;
    FILE *file;
    size_t length;
    int read_failed;

    if (path == NULL || path[0] != '/' || version == NULL) {
        return DF_ERR_INVALID;
    }
    *version = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? DF_OK : DF_ERR_IO;
    }
    length = fread(contents, 1, DF_GVS_SYNC_STATE_MAX_BYTES, file);
    read_failed = ferror(file) ||
                  (length == DF_GVS_SYNC_STATE_MAX_BYTES && fgetc(file) != EOF);
    if (fclose(file) != 0 || read_failed) {
        return DF_ERR_IO;
    }
    contents[length] = '\0';
    if (sscanf(contents,
               " config state 'sync' option version '%u' %c",
               &parsed, &trailing) != 1 || parsed > 60000U) {
        return DF_ERR_INVALID;
    }
    *version = (uint16_t)parsed;
    return DF_OK;
}

int df_gvs_sync_state_save(const char *path, uint16_t version) {
    char temporary[DF_GVS_SYNC_STATE_PATH_MAX];
    char contents[96];
    size_t path_length;
    size_t content_length;
    size_t offset = 0;
    int descriptor;
    int content_length_int;

    if (path == NULL || path[0] != '/' || version > 60000U) {
        return DF_ERR_INVALID;
    }
    path_length = strlen(path);
    if (path_length + sizeof(".tmp") > sizeof(temporary)) {
        return DF_ERR_INVALID;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));
    content_length_int = snprintf(contents, sizeof(contents),
                                  "config state 'sync'\n\toption version '%u'\n",
                                  (unsigned)version);
    if (content_length_int < 1 ||
        (size_t)content_length_int >= sizeof(contents)) {
        return DF_ERR_INVALID;
    }
    content_length = (size_t)content_length_int;
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
                      0600);
    if (descriptor < 0) {
        return DF_ERR_IO;
    }
    if (fchmod(descriptor, 0600) != 0) {
        (void)close(descriptor);
        (void)unlink(temporary);
        return DF_ERR_IO;
    }
    while (offset < content_length) {
        ssize_t written = write(descriptor, contents + offset,
                                content_length - offset);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            (void)close(descriptor);
            (void)unlink(temporary);
            return DF_ERR_IO;
        }
        offset += (size_t)written;
    }
    if (fsync(descriptor) != 0) {
        (void)close(descriptor);
        (void)unlink(temporary);
        return DF_ERR_IO;
    }
    if (close(descriptor) != 0) {
        (void)unlink(temporary);
        return DF_ERR_IO;
    }
    if (rename(temporary, path) != 0) {
        (void)unlink(temporary);
        return DF_ERR_IO;
    }
    return DF_OK;
}
