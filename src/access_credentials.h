#ifndef DOORFAST_ACCESS_CREDENTIALS_H
#define DOORFAST_ACCESS_CREDENTIALS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_ACCESS_CREDENTIAL_MAX_RECORDS 64U

enum df_station_unlock_mode {
    DF_STATION_UNLOCK_INHERIT = 0,
    DF_STATION_UNLOCK_DIRECT,
    DF_STATION_UNLOCK_DISABLED,
};

enum df_access_credential_source {
    DF_ACCESS_CREDENTIAL_SOURCE_NONE = 0,
    DF_ACCESS_CREDENTIAL_SOURCE_GLOBAL,
    DF_ACCESS_CREDENTIAL_SOURCE_STATION,
};

struct df_access_credential_record {
    char station_id[33];
    enum df_station_unlock_mode mode;
    bool material_configured;
    uint8_t material[8];
};

struct df_access_credentials {
    struct df_access_credential_record items[DF_ACCESS_CREDENTIAL_MAX_RECORDS];
    size_t count;
};

struct df_station;

int df_access_credentials_load(struct df_access_credentials *, const char *path);
int df_access_credentials_resolve(const struct df_access_credentials *,
    const struct df_station *, const char *global_material, uint8_t material[8],
    enum df_station_unlock_mode *effective_mode,
    enum df_access_credential_source *source);
int df_access_credentials_populate_station_metadata(
    const struct df_access_credentials *, struct df_station *);
int df_access_credentials_set(const char *path, const char *station_id,
    enum df_station_unlock_mode mode, const char *material_hex);
int df_access_credentials_clear(const char *path, const char *station_id);
void df_access_credentials_destroy(struct df_access_credentials *);

#endif
