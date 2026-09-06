#include "discovery.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int df_copy_part(char *dst, size_t dst_size, const char *start, size_t length) {
    if (length == 0 || length >= dst_size) {
        return DF_ERR_INVALID;
    }
    memcpy(dst, start, length);
    dst[length] = '\0';
    return DF_OK;
}

int df_endpoint_parse(const char *text, struct df_endpoint *endpoint) {
    const char *at;
    const char *colon;
    const char *host_colon;
    char *end;
    long port;

    if (text == NULL || endpoint == NULL || (at = strchr(text, '@')) == NULL ||
        (host_colon = strrchr(at + 1, ':')) == NULL) {
        return DF_ERR_INVALID;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    colon = memchr(text, ':', (size_t)(at - text));
    if (colon == NULL) {
        if (df_copy_part(endpoint->id, sizeof(endpoint->id), text, (size_t)(at - text)) != DF_OK) {
            return DF_ERR_INVALID;
        }
    } else {
        if (df_copy_part(endpoint->id, sizeof(endpoint->id), text, (size_t)(colon - text)) != DF_OK ||
            df_copy_part(endpoint->credential, sizeof(endpoint->credential), colon + 1,
                         (size_t)(at - colon - 1)) != DF_OK) {
            return DF_ERR_INVALID;
        }
    }
    if (df_copy_part(endpoint->host, sizeof(endpoint->host), at + 1,
                     (size_t)(host_colon - at - 1)) != DF_OK) {
        return DF_ERR_INVALID;
    }
    port = strtol(host_colon + 1, &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
        return DF_ERR_INVALID;
    }
    endpoint->port = (uint16_t)port;
    return DF_OK;
}

enum df_discovery_result df_discovery_observe(const struct df_event *event,
                                              struct df_candidate_store *store) {
    unsigned int index;
    struct df_candidate *candidate;

    if (event == NULL || store == NULL || event->remote_host[0] == '\0' || event->remote_port == 0) {
        return DF_DISCOVERY_INVALID;
    }
    for (index = 0; index < store->count; index++) {
        candidate = &store->candidates[index];
        if (candidate->endpoint.port == event->remote_port &&
            strcmp(candidate->endpoint.host, event->remote_host) == 0) {
            return DF_DISCOVERY_DUPLICATE;
        }
    }
    if (store->count == DF_MAX_DISCOVERY_CANDIDATES) {
        return DF_DISCOVERY_FULL;
    }
    candidate = &store->candidates[store->count++];
    memset(candidate, 0, sizeof(*candidate));
    (void)strncpy(candidate->endpoint.id, "unknown", sizeof(candidate->endpoint.id) - 1);
    (void)strncpy(candidate->endpoint.host, event->remote_host, sizeof(candidate->endpoint.host) - 1);
    candidate->endpoint.port = event->remote_port;
    return DF_DISCOVERY_CANDIDATE;
}

int df_candidate_approve(struct df_candidate *candidate) {
    if (candidate == NULL || candidate->endpoint.host[0] == '\0' || candidate->endpoint.port == 0) {
        return DF_ERR_INVALID;
    }
    candidate->approved = true;
    return DF_OK;
}
