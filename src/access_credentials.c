#include "access_credentials.h"

#include "station_registry.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DF_CRED_LINE_MAX 160U
#define DF_CRED_FILE_MAX 16384U

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_material(const char *text, uint8_t out[8]) {
    size_t i;
    if (text == NULL || strlen(text) != 16U) return DF_ERR_INVALID;
    for (i = 0; i < 8U; i++) {
        int high = hex_value(text[i * 2U]);
        int low = hex_value(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return DF_ERR_INVALID;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return DF_OK;
}

static int valid_id(const char *id) {
    size_t i, n;
    if (id == NULL) return 0;
    n = strlen(id);
    if (n == 0U || n > 32U || id[0] < 'a' || id[0] > 'z') return 0;
    for (i = 1U; i < n; i++) {
        if (!((id[i] >= 'a' && id[i] <= 'z') ||
              (id[i] >= '0' && id[i] <= '9') || id[i] == '_')) return 0;
    }
    return 1;
}

static int parse_mode(const char *text, enum df_station_unlock_mode *mode) {
    if (strcmp(text, "inherit") == 0) *mode = DF_STATION_UNLOCK_INHERIT;
    else if (strcmp(text, "direct") == 0) *mode = DF_STATION_UNLOCK_DIRECT;
    else if (strcmp(text, "disabled") == 0) *mode = DF_STATION_UNLOCK_DISABLED;
    else return DF_ERR_INVALID;
    return DF_OK;
}

void df_access_credentials_destroy(struct df_access_credentials *credentials) {
    if (credentials == NULL) return;
    memset(credentials, 0, sizeof(*credentials));
}

int df_access_credentials_load(struct df_access_credentials *credentials,
                               const char *path) {
    FILE *file;
    struct stat st;
    char line[DF_CRED_LINE_MAX];
    size_t bytes = 0U;
    struct df_access_credentials parsed = {0};
    if (credentials == NULL || path == NULL) return DF_ERR_INVALID;
    file = fopen(path, "rb");
    if (file == NULL) return DF_ERR_IO;
    if (fstat(fileno(file), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size > (off_t)DF_CRED_FILE_MAX || st.st_uid !=
#ifdef DF_ALLOW_TEST_ROOT
        (uid_t)geteuid() ||
#else
        0 ||
#endif
        (st.st_mode & 0777) != 0600) {
        errno = EINVAL;
        fclose(file); return DF_ERR_INVALID;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char id[33], mode_text[16], material_text[32], extra[2];
        struct df_access_credential_record *record;
        int fields;
        bytes += strlen(line);
        if (bytes > DF_CRED_FILE_MAX || parsed.count >= DF_ACCESS_CREDENTIAL_MAX_RECORDS)
            { fclose(file); return DF_ERR_INVALID; }
        {
            char *cursor = line;
            while (isspace((unsigned char)*cursor)) cursor++;
            if (*cursor == '\0' || *cursor == '#') continue;
        }
        fields = sscanf(line, "%32s %15s %31s %1s", id, mode_text,
                        material_text, extra);
        {
            enum df_station_unlock_mode ignored;
            if (fields < 2 || fields > 3 || !valid_id(id) ||
                parse_mode(mode_text, &ignored) != DF_OK) {
                fclose(file); return DF_ERR_INVALID;
            }
        }
        record = &parsed.items[parsed.count];
        if (parse_mode(mode_text, &record->mode) != DF_OK) {
            fclose(file); return DF_ERR_INVALID;
        }
        if (fields == 3) {
            if (record->mode != DF_STATION_UNLOCK_DIRECT ||
                parse_material(material_text, record->material) != DF_OK) {
                fclose(file); return DF_ERR_INVALID;
            }
            record->material_configured = true;
        } else if (record->mode == DF_STATION_UNLOCK_DIRECT) {
            fclose(file); return DF_ERR_INVALID;
        }
        for (size_t i = 0; i < parsed.count; i++)
            if (strcmp(parsed.items[i].station_id, id) == 0) {
                fclose(file); return DF_ERR_INVALID;
            }
        (void)snprintf(record->station_id, sizeof(record->station_id), "%s", id);
        parsed.count++;
    }
    if (ferror(file) != 0) { fclose(file); return DF_ERR_IO; }
    fclose(file);
    *credentials = parsed;
    return DF_OK;
}

int df_access_credentials_resolve(const struct df_access_credentials *credentials,
    const struct df_station *station, const char *global_material,
    uint8_t material[8], enum df_station_unlock_mode *effective_mode,
    enum df_access_credential_source *source) {
    const struct df_access_credential_record *record = NULL;
    size_t i;
    if (credentials == NULL || station == NULL || material == NULL ||
        effective_mode == NULL || source == NULL) return DF_ERR_INVALID;
    memset(material, 0, 8U);
    *effective_mode = station->unlock_mode;
    *source = DF_ACCESS_CREDENTIAL_SOURCE_NONE;
    for (i = 0; i < credentials->count; i++)
        if (strcmp(credentials->items[i].station_id, station->id) == 0) {
            record = &credentials->items[i]; break;
        }
    if (station->unlock_mode == DF_STATION_UNLOCK_DISABLED) return DF_ERR_INVALID;
    if (station->unlock_mode == DF_STATION_UNLOCK_DIRECT) {
        if (record != NULL && record->mode == DF_STATION_UNLOCK_DIRECT) {
            if (!record->material_configured) return DF_ERR_INVALID;
            memcpy(material, record->material, 8U);
            *source = DF_ACCESS_CREDENTIAL_SOURCE_STATION;
            return DF_OK;
        }
        return DF_ERR_INVALID;
    }
    if (station->unlock_mode != DF_STATION_UNLOCK_INHERIT) return DF_ERR_INVALID;
    if (global_material == NULL || global_material[0] == '\0' ||
        parse_material(global_material, material) != DF_OK) return DF_ERR_INVALID;
    *effective_mode = DF_STATION_UNLOCK_INHERIT;
    *source = DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL;
    return DF_OK;
}

int df_access_credentials_populate_station_metadata(
    const struct df_access_credentials *credentials, struct df_station *station) {
    size_t i;
    if (credentials == NULL || station == NULL) return DF_ERR_INVALID;
    station->unlock_material_configured = false;
    for (i = 0; i < credentials->count; i++) {
        if (strcmp(credentials->items[i].station_id, station->id) == 0) {
            station->unlock_material_configured =
                credentials->items[i].mode == DF_STATION_UNLOCK_DIRECT &&
                credentials->items[i].material_configured;
            return DF_OK;
        }
    }
    return DF_OK;
}

static int write_records(const char *path, const struct df_access_credentials *credentials) {
    char temp[512];
    int fd, result = DF_OK;
    FILE *file;
    if (snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid()) >=
        (int)sizeof(temp)) return DF_ERR_INVALID;
    fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return DF_ERR_IO;
    file = fdopen(fd, "wb");
    if (file == NULL) { close(fd); unlink(temp); return DF_ERR_IO; }
    if (fchmod(fd, 0600) != 0) result = DF_ERR_IO;
    for (size_t i = 0; result == DF_OK && i < credentials->count; i++) {
        const struct df_access_credential_record *r = &credentials->items[i];
        if (r->mode == DF_STATION_UNLOCK_DIRECT)
            fprintf(file, "%s direct %02x%02x%02x%02x%02x%02x%02x%02x\n",
                r->station_id, r->material[0], r->material[1], r->material[2],
                r->material[3], r->material[4], r->material[5], r->material[6], r->material[7]);
        else fprintf(file, "%s %s\n", r->station_id,
            r->mode == DF_STATION_UNLOCK_DISABLED ? "disabled" : "inherit");
        if (ferror(file)) result = DF_ERR_IO;
    }
    if (fflush(file) != 0 || fsync(fd) != 0) result = DF_ERR_IO;
    if (fclose(file) != 0) result = DF_ERR_IO;
    if (result == DF_OK && rename(temp, path) != 0) result = DF_ERR_IO;
    if (result != DF_OK) unlink(temp);
    return result;
}

int df_access_credentials_set(const char *path, const char *station_id,
    enum df_station_unlock_mode mode, const char *material_hex) {
    struct df_access_credentials credentials = {0};
    struct df_access_credential_record *record;
    if (path == NULL || !valid_id(station_id) ||
        (mode != DF_STATION_UNLOCK_INHERIT && mode != DF_STATION_UNLOCK_DIRECT &&
         mode != DF_STATION_UNLOCK_DISABLED) ||
        (mode == DF_STATION_UNLOCK_DIRECT && parse_material(material_hex,
            (uint8_t[8]){0}) != DF_OK)) return DF_ERR_INVALID;
    if (access(path, F_OK) == 0 && df_access_credentials_load(&credentials, path) != DF_OK)
        return DF_ERR_INVALID;
    for (size_t i = 0; i < credentials.count; i++)
        if (strcmp(credentials.items[i].station_id, station_id) == 0) {
            record = &credentials.items[i]; goto found;
        }
    if (credentials.count >= DF_ACCESS_CREDENTIAL_MAX_RECORDS) return DF_ERR_INVALID;
    record = &credentials.items[credentials.count++];
found:
    memset(record, 0, sizeof(*record));
    (void)snprintf(record->station_id, sizeof(record->station_id), "%s", station_id);
    record->mode = mode;
    if (mode == DF_STATION_UNLOCK_DIRECT) {
        (void)parse_material(material_hex, record->material);
        record->material_configured = true;
    }
    return write_records(path, &credentials);
}

int df_access_credentials_clear(const char *path, const char *station_id) {
    struct df_access_credentials credentials = {0};
    if (path == NULL || !valid_id(station_id)) return DF_ERR_INVALID;
    if (access(path, F_OK) != 0) return DF_OK;
    if (df_access_credentials_load(&credentials, path) != DF_OK) return DF_ERR_INVALID;
    for (size_t i = 0; i < credentials.count; i++)
        if (strcmp(credentials.items[i].station_id, station_id) == 0) {
            credentials.items[i] = credentials.items[credentials.count - 1U];
            credentials.count--; break;
        }
    return write_records(path, &credentials);
}
