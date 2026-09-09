#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gvs_peer_sim.h"

#define DF_GVS_TEST_PORT 18300

static int make_frame(const char *scenario, struct df_gvs_peer_sim **sim,
                      const uint8_t **frame, size_t *length) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_presence_action action = {
        .type = DF_GVS_PRESENCE_SYNC_ASK_ACTION,
        .target = {0x61, 2, 1, 1, 1, 1},
        .round = 1,
    };

    if (strcmp(scenario, "sync-reply") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_MAINTAINER_LOSS,
                                   local, 0) != DF_OK ||
            df_gvs_peer_sim_emit(&action, *sim) != DF_OK) {
            return DF_ERR_IO;
        }
    } else if (strcmp(scenario, "peer-online") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_NO_PEER, local, 0) !=
                DF_OK ||
            df_gvs_peer_sim_make_peer_online(*sim) != DF_OK) {
            return DF_ERR_IO;
        }
    } else if (strcmp(scenario, "peer-probe") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_NO_PEER, local, 0) !=
                DF_OK ||
            df_gvs_peer_sim_make_peer_probe(*sim) != DF_OK) {
            return DF_ERR_IO;
        }
    } else if (strcmp(scenario, "call-local") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_NO_PEER, local, 0) !=
                DF_OK ||
            df_gvs_peer_sim_make_call(*sim, local) != DF_OK) {
            return DF_ERR_IO;
        }
    } else if (strcmp(scenario, "periodic-sync") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_MAINTAINER_LOSS,
                                   local, 0) != DF_OK ||
            df_gvs_peer_sim_make_periodic_sync(*sim) != DF_OK) {
            return DF_ERR_IO;
        }
    } else if (strcmp(scenario, "normal-update") == 0) {
        if (df_gvs_peer_sim_create(sim, DF_GVS_SIM_MAINTAINER_LOSS,
                                   local, 0) != DF_OK ||
            df_gvs_peer_sim_make_normal_update(*sim) != DF_OK) {
            return DF_ERR_IO;
        }
    } else {
        return DF_ERR_INVALID;
    }
    return df_gvs_peer_sim_next_frame(*sim, frame, length);
}

static int send_local(const uint8_t *frame, size_t length) {
    const struct sockaddr_in target = {
        .sin_family = AF_INET,
        .sin_port = htons(DF_GVS_TEST_PORT),
        .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)},
    };
    ssize_t sent;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        return DF_ERR_IO;
    }
    sent = sendto(socket_fd, frame, length, 0,
                  (const struct sockaddr *)&target, sizeof(target));
    if (close(socket_fd) != 0 || sent < 0 || (size_t)sent != length) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

int main(int argc, char **argv) {
    struct df_gvs_peer_sim *sim = NULL;
    const uint8_t *frame = NULL;
    size_t length = 0;
    int result;

    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        fprintf(stderr,
                "usage: %s --scenario "
                "sync-reply|peer-online|peer-probe|periodic-sync|"
                "normal-update|call-local\n",
                argv[0]);
        return 2;
    }
    result = make_frame(argv[2], &sim, &frame, &length);
    if (result == DF_ERR_INVALID) {
        fprintf(stderr, "unknown scenario: %s\n", argv[2]);
        return 2;
    }
    if (result != DF_OK || send_local(frame, length) != DF_OK) {
        fprintf(stderr, "failed to send built-in scenario\n");
        df_gvs_peer_sim_destroy(sim);
        return 1;
    }
    printf("{\"scenario\":\"%s\",\"target\":\"127.0.0.1:18300\","
           "\"bytes\":%zu}\n", argv[2], length);
    df_gvs_peer_sim_destroy(sim);
    return 0;
}
