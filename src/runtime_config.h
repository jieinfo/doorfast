#ifndef DOORFAST_RUNTIME_CONFIG_H
#define DOORFAST_RUNTIME_CONFIG_H

#include "config.h"

#define DF_RUNTIME_BRAND_MAX 16
#define DF_RUNTIME_INTERFACE_MAX 64
#define DF_RUNTIME_ADDRESS_MAX 64
#define DF_RUNTIME_IPV4_MAX 16
#define DF_RUNTIME_PATH_MAX 256
#define DF_RUNTIME_MEDIA_STATION_ADDRESS_MAX 18
#define DF_RUNTIME_MEDIA_HOST_MAX 64
#define DF_RUNTIME_MEDIA_STREAM_MAX 65
#define DF_RUNTIME_MEDIA_USERNAME_MAX 33
#define DF_RUNTIME_MEDIA_RELAY_URL_MAX 256

struct df_runtime_config {
    struct df_config config;
    char access_material[17];
    char brand[DF_RUNTIME_BRAND_MAX];
    char gvs_interface[DF_RUNTIME_INTERFACE_MAX];
    char passive_interface[DF_RUNTIME_INTERFACE_MAX];
    char host_interface[DF_RUNTIME_INTERFACE_MAX];
    char gvs_local_address[DF_RUNTIME_ADDRESS_MAX];
    char indoor_ipaddr[DF_RUNTIME_IPV4_MAX];
    char indoor_netmask[DF_RUNTIME_IPV4_MAX];
    char uplink_interface[DF_RUNTIME_INTERFACE_MAX];
    char sync_state_path[DF_RUNTIME_PATH_MAX];
    char media_station_address[DF_RUNTIME_MEDIA_STATION_ADDRESS_MAX];
    char media_station_ipv4[DF_RUNTIME_IPV4_MAX];
    char media_go2rtc_host[DF_RUNTIME_MEDIA_HOST_MAX];
    char media_stream_name[DF_RUNTIME_MEDIA_STREAM_MAX];
    char media_rtsp_username[DF_RUNTIME_MEDIA_USERNAME_MAX];
    char media_relay_url[DF_RUNTIME_MEDIA_RELAY_URL_MAX];
};

int df_runtime_config_parse(const char *uci_text, struct df_runtime_config *runtime);
int df_runtime_config_load(const char *path, struct df_runtime_config *runtime);

#endif
