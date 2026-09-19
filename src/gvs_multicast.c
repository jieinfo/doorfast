#include "gvs_multicast.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int df_gvs_multicast_prepare_group(struct df_gvs_multicast *membership,
                                   const char *group,
                                   const char *local_address) {
    struct in_addr parsed_group;
    struct in_addr parsed_local;

    if (membership == NULL || group == NULL || local_address == NULL ||
        inet_pton(AF_INET, group, &parsed_group) != 1 ||
        !IN_MULTICAST(ntohl(parsed_group.s_addr)) ||
        inet_pton(AF_INET, local_address, &parsed_local) != 1)
        return DF_ERR_INVALID;
    memset(membership, 0, sizeof(*membership));
    membership->fd = -1;
    membership->port = DF_GVS_MULTICAST_PORT;
    if (snprintf(membership->group, sizeof(membership->group), "%s", group) >=
            (int)sizeof(membership->group) ||
        snprintf(membership->local_address, sizeof(membership->local_address),
                 "%s", local_address) >= (int)sizeof(membership->local_address)) {
        memset(membership, 0, sizeof(*membership));
        membership->fd = -1;
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

int df_gvs_multicast_prepare(struct df_gvs_multicast *membership,
                             const uint8_t identity[6],
                             const char *local_address) {
    char group[DF_GVS_IPV4_TEXT_SIZE];

    if (identity == NULL ||
        df_gvs_identity_multicast_ip(identity, group) != DF_OK)
        return DF_ERR_INVALID;
    return df_gvs_multicast_prepare_group(membership, group, local_address);
}

static int df_gvs_multicast_join(struct df_gvs_multicast *membership) {
    struct sockaddr_in local = {0};
    struct ip_mreq request = {0};
    int reuse = 1;

    membership->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (membership->fd < 0) return DF_ERR_IO;
    local.sin_family = AF_INET;
    local.sin_port = htons(membership->port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(membership->fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(reuse)) != 0 ||
        bind(membership->fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        inet_pton(AF_INET, membership->group, &request.imr_multiaddr) != 1 ||
        inet_pton(AF_INET, membership->local_address,
                  &request.imr_interface) != 1 ||
        setsockopt(membership->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request,
                   sizeof(request)) != 0) {
        df_gvs_multicast_close(membership);
        return DF_ERR_IO;
    }
    membership->joined = true;
    return DF_OK;
}

int df_gvs_multicast_open_group(struct df_gvs_multicast *membership,
                                const char *group,
                                const char *local_address) {
    if (df_gvs_multicast_prepare_group(membership, group, local_address) != DF_OK)
        return DF_ERR_INVALID;
    return df_gvs_multicast_join(membership);
}

int df_gvs_multicast_open(struct df_gvs_multicast *membership,
                          const uint8_t identity[6],
                          const char *local_address) {
    if (df_gvs_multicast_prepare(membership, identity, local_address) != DF_OK)
        return DF_ERR_INVALID;
    return df_gvs_multicast_join(membership);
}

void df_gvs_multicast_close(struct df_gvs_multicast *membership) {
    struct ip_mreq request = {0};

    if (membership == NULL) return;
    if (membership->fd >= 0 && membership->joined &&
        inet_pton(AF_INET, membership->group, &request.imr_multiaddr) == 1 &&
        inet_pton(AF_INET, membership->local_address,
                  &request.imr_interface) == 1)
        (void)setsockopt(membership->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                         &request, sizeof(request));
    if (membership->fd >= 0) (void)close(membership->fd);
    membership->fd = -1;
    membership->joined = false;
}
