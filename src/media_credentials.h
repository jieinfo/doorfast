#ifndef DOORFAST_MEDIA_CREDENTIALS_H
#define DOORFAST_MEDIA_CREDENTIALS_H

#include <stdbool.h>

#include "doorfast.h"

#define DF_MEDIA_CREDENTIAL_VALUE_MAX 256U

struct df_media_credentials {
    char rtsp_password[DF_MEDIA_CREDENTIAL_VALUE_MAX];
};

struct df_media_credentials_update {
    bool set_rtsp_password;
    bool clear_rtsp_password;
    const char *rtsp_password;
};

struct df_media_credentials_status {
    bool rtsp_password_set;
};

int df_media_credentials_load(const char *path, struct df_media_credentials *out);
int df_media_credentials_write(const char *path,
    const struct df_media_credentials *replacement,
    const struct df_media_credentials_update *update);
void df_media_credentials_status(const struct df_media_credentials *credentials,
    struct df_media_credentials_status *status);

#endif
