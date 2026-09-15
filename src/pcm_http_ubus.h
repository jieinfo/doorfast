#ifndef DF_PCM_HTTP_UBUS_H
#define DF_PCM_HTTP_UBUS_H
#include "pcm_http.h"
int df_pcm_http_ubus_status(struct df_pcm_http_status *, void *);
int df_pcm_http_ubus_send(const char *, uint64_t, const int16_t *, size_t, void *);
#endif
