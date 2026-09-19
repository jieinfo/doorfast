#include "media_credentials.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DF_MEDIA_CREDENTIALS_FILE_MAX 1024U
#define DF_MEDIA_CREDENTIALS_CONTENT_MAX (DF_MEDIA_CREDENTIAL_VALUE_MAX + 16U)

static bool df_media_credential_value_is_valid(const char *value) {
    size_t index;
    size_t length;

    if (value == NULL) return false;
    length = strlen(value);
    if (length >= DF_MEDIA_CREDENTIAL_VALUE_MAX) return false;
    for (index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];

        if (character < 0x20U || character > 0x7eU ||
            character == '\n' || character == '\r') return false;
    }
    return true;
}

static bool df_media_credentials_path_is_valid(const char *path) {
    size_t length;

    if (path == NULL || path[0] != '/') return false;
    length = strlen(path);
    return length > 1U && path[length - 1U] != '/' &&
           length + sizeof(".tmp.XXXXXX") <= DF_MEDIA_CREDENTIALS_FILE_MAX;
}

static int df_media_credentials_sync_parent(const char *path) {
    char parent[DF_MEDIA_CREDENTIALS_FILE_MAX];
    const char *slash = strrchr(path, '/');
    struct stat info;
    int descriptor;
    size_t length;

    if (slash == NULL) return DF_ERR_INVALID;
    length = slash == path ? 1U : (size_t)(slash - path);
    if (length >= sizeof(parent)) return DF_ERR_INVALID;
    memcpy(parent, path, length);
    parent[length] = '\0';
    descriptor = open(parent, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return DF_ERR_IO;
    if (fstat(descriptor, &info) != 0 || !S_ISDIR(info.st_mode)) {
        (void)close(descriptor);
        return DF_ERR_IO;
    }
    if (fsync(descriptor) != 0) {
        (void)close(descriptor);
        return DF_ERR_IO;
    }
    if (close(descriptor) != 0) return DF_ERR_IO;
    return DF_OK;
}

static int df_media_credentials_copy(char destination[DF_MEDIA_CREDENTIAL_VALUE_MAX],
                                     const char *value) {
    size_t length;

    if (!df_media_credential_value_is_valid(value)) return DF_ERR_INVALID;
    length = strlen(value);
    memcpy(destination, value, length + 1U);
    return DF_OK;
}

static int df_media_credentials_parse(char *contents,
                                      struct df_media_credentials *credentials) {
    char *line = contents;
    bool saw_rtsp_password = false;

    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');

        if (next != NULL) *next = '\0';
        if (strncmp(line, "rtsp_password=", sizeof("rtsp_password=") - 1U) == 0) {
            if (saw_rtsp_password ||
                df_media_credentials_copy(credentials->rtsp_password,
                    line + sizeof("rtsp_password=") - 1U) != DF_OK) return DF_ERR_INVALID;
            saw_rtsp_password = true;
        } else if (*line != '\0') {
            return DF_ERR_INVALID;
        }
        line = next == NULL ? NULL : next + 1U;
    }
    return DF_OK;
}

int df_media_credentials_load(const char *path, struct df_media_credentials *out) {
    char contents[DF_MEDIA_CREDENTIALS_CONTENT_MAX + 1U];
    struct stat info;
    size_t length = 0;
    int descriptor;

    if (!df_media_credentials_path_is_valid(path) || out == NULL) return DF_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return errno == ENOENT ? DF_OK : DF_ERR_IO;
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0777) != 0600) {
        (void)close(descriptor);
        return DF_ERR_INVALID;
    }
    while (length < DF_MEDIA_CREDENTIALS_CONTENT_MAX) {
        ssize_t read_length = read(descriptor, contents + length,
                                   DF_MEDIA_CREDENTIALS_CONTENT_MAX - length);

        if (read_length < 0 && errno == EINTR) continue;
        if (read_length < 0) {
            (void)close(descriptor);
            return DF_ERR_IO;
        }
        if (read_length == 0) break;
        length += (size_t)read_length;
    }
    if (length == DF_MEDIA_CREDENTIALS_CONTENT_MAX || close(descriptor) != 0)
        return DF_ERR_IO;
    contents[length] = '\0';
    return df_media_credentials_parse(contents, out);
}

static int df_media_credentials_apply(struct df_media_credentials *credentials,
                                      const struct df_media_credentials_update *update) {
    if (update == NULL) return DF_OK;
    if (update->set_rtsp_password && update->clear_rtsp_password)
        return DF_ERR_INVALID;
    if (update->clear_rtsp_password) credentials->rtsp_password[0] = '\0';
    else if (update->set_rtsp_password && update->rtsp_password != NULL &&
             update->rtsp_password[0] != '\0' &&
             df_media_credentials_copy(credentials->rtsp_password,
                                       update->rtsp_password) != DF_OK) return DF_ERR_INVALID;
    return DF_OK;
}

int df_media_credentials_write(const char *path,
    const struct df_media_credentials *replacement,
    const struct df_media_credentials_update *update) {
    char temporary[DF_MEDIA_CREDENTIALS_FILE_MAX];
    char contents[DF_MEDIA_CREDENTIALS_CONTENT_MAX];
    struct df_media_credentials credentials;
    int descriptor;
    int content_length;
    size_t offset = 0;

    if (!df_media_credentials_path_is_valid(path)) return DF_ERR_INVALID;
    if (replacement != NULL) {
        if (df_media_credentials_copy(credentials.rtsp_password,
                                      replacement->rtsp_password) != DF_OK)
            return DF_ERR_INVALID;
    } else if (df_media_credentials_load(path, &credentials) != DF_OK) {
        return DF_ERR_IO;
    }
    if (df_media_credentials_apply(&credentials, update) != DF_OK) return DF_ERR_INVALID;
    content_length = snprintf(contents, sizeof(contents), "rtsp_password=%s\n",
                              credentials.rtsp_password);
    if (content_length < 0 || (size_t)content_length >= sizeof(contents) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) < 0) return DF_ERR_INVALID;
    descriptor = mkstemp(temporary);
    if (descriptor < 0) return DF_ERR_IO;
    if (fchmod(descriptor, 0600) != 0) goto fail;
    while (offset < (size_t)content_length) {
        ssize_t written = write(descriptor, contents + offset,
                                (size_t)content_length - offset);

        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) goto fail;
        offset += (size_t)written;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0 ||
        rename(temporary, path) != 0) {
        (void)unlink(temporary);
        return DF_ERR_IO;
    }
    return df_media_credentials_sync_parent(path);
fail:
    (void)close(descriptor);
    (void)unlink(temporary);
    return DF_ERR_IO;
}

void df_media_credentials_status(const struct df_media_credentials *credentials,
                                 struct df_media_credentials_status *status) {
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (credentials == NULL) return;
    status->rtsp_password_set = credentials->rtsp_password[0] != '\0';
}
