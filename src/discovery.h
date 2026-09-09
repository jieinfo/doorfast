#ifndef DOORFAST_DISCOVERY_H
#define DOORFAST_DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "doorfast.h"
#include "event.h"

enum { DF_MAX_DISCOVERY_CANDIDATES = 16 };

struct df_endpoint {
    char id[64];
    char credential[128];
    char host[64];
    uint16_t port;
};

struct df_candidate {
    struct df_endpoint endpoint;
    bool approved;
};

struct df_candidate_store {
    struct df_candidate candidates[DF_MAX_DISCOVERY_CANDIDATES];
    unsigned int count;
};

enum df_discovery_result {
    DF_DISCOVERY_INVALID = 0,
    DF_DISCOVERY_CANDIDATE,
    DF_DISCOVERY_DUPLICATE,
    DF_DISCOVERY_FULL,
};

int df_endpoint_parse(const char *text, struct df_endpoint *endpoint);
enum df_discovery_result df_discovery_observe(const struct df_event *event,
                                              struct df_candidate_store *store);
int df_candidate_approve(struct df_candidate *candidate);

#endif
