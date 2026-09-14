#include "gvs_udp_sender.h"
#include "gvs_identity.h"
#include "gvs_media.h"
#include "gvs_packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const struct df_gvs_observed_route *df_gvs_udp_find_route(
    const struct df_gvs_udp_sender *sender, const uint8_t peer[6]) {
    size_t index;

    for (index = 0; index < DF_GVS_OBSERVED_ROUTE_CAPACITY; index++) {
        if (sender->observed_routes[index].valid &&
            memcmp(sender->observed_routes[index].peer, peer, 6) == 0)
            return &sender->observed_routes[index];
    }
    return NULL;
}

static int df_gvs_udp_resolve_destination(
    const struct df_gvs_udp_sender *sender, const uint8_t peer[6],
    struct sockaddr_in *destination) {
    const struct df_gvs_observed_route *route;
    char host[INET_ADDRSTRLEN];

    if (sender == NULL || peer == NULL || destination == NULL)
        return DF_ERR_INVALID;
    *destination = sender->peer;
    route = df_gvs_udp_find_route(sender, peer);
    if (route != NULL) {
        destination->sin_addr.s_addr = route->ipv4;
        return DF_OK;
    }
    if (df_gvs_identity_unicast_ip(peer, host) != DF_OK ||
        inet_pton(AF_INET, host, &destination->sin_addr) != 1)
        return DF_ERR_INVALID;
    return DF_OK;
}

int df_gvs_udp_sender_open(struct df_gvs_udp_sender *sender, const char *host,
    uint16_t port, df_gvs_header_provider_fn provider, void *context) {
    if (sender == NULL || host == NULL || provider == NULL || port == 0U)
        return DF_ERR_INVALID;
    memset(sender, 0, sizeof(*sender));
    sender->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->fd < 0) return DF_ERR_IO;
    sender->peer.sin_family = AF_INET;
    sender->peer.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &sender->peer.sin_addr) != 1) {
        close(sender->fd); sender->fd = -1; return DF_ERR_INVALID;
    }
    sender->provide_fields = provider;
    sender->fields_context = context;
    return DF_OK;
}

void df_gvs_udp_sender_close(struct df_gvs_udp_sender *sender) {
    if (sender == NULL) return;
    if (sender->fd >= 0) close(sender->fd);
    sender->fd = -1;
}

enum df_gvs_send_attempt_result df_gvs_udp_send_attempt(
    const struct df_gvs_call_command *command, unsigned attempt,
    uint64_t completion_id, void *context) {
    struct df_gvs_udp_sender *sender = context;
    uint8_t frame[DF_GVS_CALL_COMMAND_MAX_FRAME_SIZE];
    size_t length = 0;
    ssize_t written;
    if (sender == NULL || command == NULL || sender->fd < 0 || attempt == 0U ||
        completion_id == 0U || df_gvs_call_command_serialize(command, frame,
        sizeof(frame), &length, sender->provide_fields,
        sender->fields_context) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) sender->failed++;
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    {
        struct sockaddr_in destination;
        if (df_gvs_udp_resolve_destination(
                sender, command->destination, &destination) != DF_OK) {
            if (sender->failed < UINT_MAX) sender->failed++;
            return DF_GVS_SEND_ATTEMPT_FAILURE;
        }
        written = sendto(sender->fd, frame, length, 0,
            (const struct sockaddr *)&destination, sizeof(destination));
    }
    if (written != (ssize_t)length) {
        if (sender->failed < UINT_MAX) sender->failed++;
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    if (sender->sent < UINT_MAX) sender->sent++;
    return DF_GVS_SEND_ATTEMPT_SUCCESS;
}

int df_gvs_udp_incoming_reply_emit(const struct df_gvs_incoming_reply *reply,
    struct df_gvs_udp_sender *sender) {
    uint8_t frame[DF_GVS_INCOMING_REPLY_FRAME_SIZE];
    struct sockaddr_in destination;
    size_t length = 0;
    ssize_t written;

    if (reply == NULL || sender == NULL || sender->fd < 0 ||
        df_gvs_incoming_reply_serialize(reply, frame, sizeof(frame), &length,
            sender->provide_fields, sender->fields_context) != DF_OK ||
        df_gvs_udp_resolve_destination(
            sender, reply->destination, &destination) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_INVALID;
    }
    written = sendto(sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX) sender->sent++;
    return DF_OK;
}

int df_gvs_udp_sender_observe_peer(struct df_gvs_udp_sender *sender,
    const uint8_t *packet, size_t packet_length, const uint8_t identity[6]) {
    struct df_udp_prefix prefix;
    struct df_gvs_frame frame;
    struct df_event event;
    struct df_gvs_observed_route *route;
    size_t index;

    if (sender == NULL || packet == NULL || identity == NULL ||
        df_gvs_inspect_udp_prefix(packet, packet_length, &prefix) != 1 ||
        !prefix.payload_complete || prefix.destination_port != 8300U ||
        df_gvs_frame_parse(packet + prefix.payload_offset,
            prefix.captured_payload_length, &frame, &event) != DF_OK ||
        !df_gvs_frame_is_for_identity(&frame, identity))
        return DF_ERR_INVALID;

    route = NULL;
    for (index = 0; index < DF_GVS_OBSERVED_ROUTE_CAPACITY; index++) {
        if (sender->observed_routes[index].valid &&
            memcmp(sender->observed_routes[index].peer, frame.source, 6) == 0) {
            route = &sender->observed_routes[index];
            break;
        }
    }
    if (route == NULL) {
        route = &sender->observed_routes[sender->next_observed_route];
        sender->next_observed_route = (sender->next_observed_route + 1U) %
            DF_GVS_OBSERVED_ROUTE_CAPACITY;
    }
    memcpy(route->peer, frame.source, 6);
    route->ipv4 = prefix.source_ipv4;
    route->valid = true;
    return DF_OK;
}

int df_gvs_udp_presence_emit(const struct df_gvs_presence_action *action,
    void *context) {
    struct df_gvs_udp_presence_context *ctx = context;
    uint8_t frame[DF_GVS_CONTROL_HEADER_SIZE + 3U];
    size_t length = 0;
    char host[DF_GVS_IPV4_TEXT_SIZE];
    struct sockaddr_in destination;
    ssize_t written;
    if (action == NULL || ctx == NULL || ctx->sender == NULL ||
        ctx->sender->fd < 0 || ctx->source == NULL ||
        df_gvs_presence_action_serialize(action, ctx->source, ctx->sync_version,
            frame, sizeof(frame), &length, ctx->sender->provide_fields,
            ctx->sender->fields_context) != DF_OK ||
        df_gvs_identity_unicast_ip(action->target, host) != DF_OK ||
        inet_pton(AF_INET, host, &destination.sin_addr) != 1) {
        return DF_ERR_INVALID;
    }
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(8300);
    (void)inet_pton(AF_INET, host, &destination.sin_addr);
    written = sendto(ctx->sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    return written == (ssize_t)length ? DF_OK : DF_ERR_IO;
}

int df_gvs_udp_access_emit(const struct df_gvs_access_request *reply,
    void *context) {
    struct df_gvs_udp_sender *sender = context;
    uint8_t frame[DF_GVS_ACCESS_DIRECT_FRAME_SIZE];
    struct sockaddr_in destination;
    size_t length = 0;
    ssize_t written;

    if (reply == NULL || sender == NULL || sender->fd < 0 ||
        df_gvs_access_serialize_direct(reply, frame, sizeof(frame), &length,
            sender->provide_fields, sender->fields_context) != DF_OK ||
        df_gvs_udp_resolve_destination(
            sender, reply->destination, &destination) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_INVALID;
    }
    written = sendto(sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX) sender->sent++;
    return DF_OK;
}

int df_gvs_udp_elevator_emit(const struct df_gvs_elevator_request *request,
    void *context) {
    struct df_gvs_udp_sender *sender = context;
    uint8_t frame[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
    struct sockaddr_in destination;
    size_t length = 0;
    ssize_t written;

    if (request == NULL || sender == NULL || sender->fd < 0 ||
        df_gvs_elevator_serialize(request, frame, sizeof(frame), &length,
            sender->provide_fields, sender->fields_context) != DF_OK ||
        df_gvs_udp_resolve_destination(
            sender, request->destination, &destination) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX)
            sender->failed++;
        return DF_ERR_INVALID;
    }
    written = sendto(sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (sender->failed < UINT_MAX)
            sender->failed++;
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX)
        sender->sent++;
    return DF_OK;
}

int df_gvs_udp_audio_emit(const uint8_t *frame, size_t length, void *context)
{
    struct df_gvs_udp_sender *sender = context;
    struct df_gvs_audio_packet packet;
    struct sockaddr_in destination;
    ssize_t written;

    if (sender == NULL || sender->fd < 0 ||
        df_gvs_parse_audio(frame, length, &packet) != 0 ||
        packet.payload_length == 0U ||
        length != DF_GVS_AUDIO_HEADER_LEN + packet.payload_length ||
        df_gvs_udp_resolve_destination(
            sender, packet.destination, &destination) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) {
            sender->failed++;
        }
        return DF_ERR_INVALID;
    }
    destination.sin_port = htons(8302U);
    written = sendto(sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (sender->failed < UINT_MAX) {
            sender->failed++;
        }
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX) {
        sender->sent++;
    }
    return DF_OK;
}
