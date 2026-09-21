#include "gvs_udp_sender.h"
#include "gvs_identity.h"
#include "gvs_media.h"
#include "gvs_packet.h"
#include "gvs_serialize.h"
#include "gvs_station.h"

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
            sender->current_ms >= sender->observed_routes[index].observed_ms &&
            sender->current_ms - sender->observed_routes[index].observed_ms <
                DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS &&
            memcmp(sender->observed_routes[index].peer, peer, 6) == 0)
            return &sender->observed_routes[index];
    }
    return NULL;
}

static bool df_gvs_udp_ipv4_is_unicast(uint32_t ipv4) {
    uint32_t host = ntohl(ipv4);
    unsigned first_octet = (unsigned)(host >> 24U);

    return host != 0U && host != UINT32_MAX && first_octet > 0U &&
        first_octet < 224U;
}

static struct df_gvs_preview_route *df_gvs_udp_preview_route_mutable(
    struct df_gvs_udp_sender *sender, const uint8_t peer[6]) {
    size_t index;

    for (index = 0U; index < DF_GVS_PREVIEW_ROUTE_CAPACITY; index++) {
        if (sender->preview_routes[index].valid &&
            memcmp(sender->preview_routes[index].peer, peer, 6U) == 0)
            return &sender->preview_routes[index];
    }
    index = sender->next_preview_route;
    sender->next_preview_route = (sender->next_preview_route + 1U) %
        DF_GVS_PREVIEW_ROUTE_CAPACITY;
    return &sender->preview_routes[index];
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

static int df_gvs_udp_sender_open_ports(struct df_gvs_udp_sender *sender,
    const char *host, uint16_t source_port, uint16_t peer_port,
    df_gvs_header_provider_fn provider, void *context) {
    struct sockaddr_in local;
    int broadcast = 1;
    int reuse = 1;

    if (sender == NULL || host == NULL || provider == NULL || peer_port == 0U)
        return DF_ERR_INVALID;
    memset(sender, 0, sizeof(*sender));
    sender->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->fd < 0) return DF_ERR_IO;
    if (setsockopt(sender->fd, SOL_SOCKET, SO_BROADCAST, &broadcast,
            sizeof(broadcast)) != 0) {
        close(sender->fd); sender->fd = -1; return DF_ERR_IO;
    }
    if (setsockopt(sender->fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
            sizeof(reuse)) != 0) {
        close(sender->fd); sender->fd = -1; return DF_ERR_IO;
    }
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(source_port);
    if (inet_pton(AF_INET, host, &local.sin_addr) != 1 ||
        bind(sender->fd, (const struct sockaddr *)&local, sizeof(local)) != 0) {
        close(sender->fd); sender->fd = -1; return DF_ERR_INVALID;
    }
    sender->peer.sin_family = AF_INET;
    sender->peer.sin_port = htons(peer_port);
    sender->peer.sin_addr.s_addr = htonl(INADDR_ANY);
    sender->provide_fields = provider;
    sender->fields_context = context;
    return DF_OK;
}

int df_gvs_udp_sender_open(struct df_gvs_udp_sender *sender, const char *host,
    uint16_t port, df_gvs_header_provider_fn provider, void *context) {
    return df_gvs_udp_sender_open_ports(sender, host, 0U, port, provider,
        context);
}

int df_gvs_udp_sender_open_bound(struct df_gvs_udp_sender *sender,
    const char *host, uint16_t source_port, uint16_t peer_port,
    df_gvs_header_provider_fn provider, void *context) {
    if (source_port == 0U) return DF_ERR_INVALID;
    return df_gvs_udp_sender_open_ports(sender, host, source_port, peer_port,
        provider, context);
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

enum df_gvs_send_attempt_result df_gvs_udp_peer_reply_send_attempt(
    const struct df_gvs_reply_queue_entry *entry, unsigned attempt,
    uint64_t completion_id, void *context) {
    struct df_gvs_udp_presence_context *ctx = context;
    uint8_t frame[DF_GVS_CONTROL_HEADER_SIZE + 6U];
    struct sockaddr_in destination;
    size_t length = 0U;
    ssize_t written;

    if (entry == NULL || attempt == 0U || completion_id == 0U || ctx == NULL ||
        ctx->sender == NULL || ctx->sender->fd < 0 || ctx->source == NULL ||
        df_gvs_peer_reply_serialize(&entry->reply, ctx->source, frame,
            sizeof(frame), &length, ctx->sender->provide_fields,
            ctx->sender->fields_context) != DF_OK ||
        length != sizeof(frame) ||
        df_gvs_udp_resolve_destination(ctx->sender, entry->reply.target,
            &destination) != DF_OK) {
        if (ctx != NULL && ctx->sender != NULL &&
            ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    written = sendto(ctx->sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
        return DF_GVS_SEND_ATTEMPT_FAILURE;
    }
    if (ctx->sender->sent < UINT_MAX) ctx->sender->sent++;
    return DF_GVS_SEND_ATTEMPT_SUCCESS;
}

int df_gvs_udp_sender_advance(struct df_gvs_udp_sender *sender,
    uint64_t now_ms) {
    size_t index;

    if (sender == NULL || now_ms < sender->current_ms) return DF_ERR_INVALID;
    sender->current_ms = now_ms;
    for (index = 0U; index < DF_GVS_OBSERVED_ROUTE_CAPACITY; index++) {
        struct df_gvs_observed_route *route = &sender->observed_routes[index];
        if (route->valid &&
            (now_ms < route->observed_ms ||
             now_ms - route->observed_ms >=
                DF_GVS_OBSERVED_ROUTE_MAX_AGE_MS))
            route->valid = false;
    }
    return DF_OK;
}

int df_gvs_udp_sender_observe_peer(struct df_gvs_udp_sender *sender,
    const uint8_t *packet, size_t packet_length, const uint8_t identity[6],
    uint64_t now_ms) {
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
        !df_gvs_frame_is_for_identity(&frame, identity) ||
        df_gvs_udp_sender_advance(sender, now_ms) != DF_OK)
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
    route->observed_ms = now_ms;
    route->valid = true;
    return DF_OK;
}

int df_gvs_udp_sender_observe_preview_route(struct df_gvs_udp_sender *sender,
    const uint8_t *packet, size_t packet_length, const uint8_t identity[6],
    uint64_t now_ms) {
    struct df_udp_prefix prefix;
    struct df_gvs_frame frame;
    struct df_event event;
    struct df_gvs_preview_route *route;

    if (sender == NULL || packet == NULL || identity == NULL ||
        df_gvs_inspect_udp_prefix(packet, packet_length, &prefix) != 1 ||
        !prefix.payload_complete || prefix.destination_port != 8300U ||
        !df_gvs_udp_ipv4_is_unicast(prefix.source_ipv4) ||
        df_gvs_frame_parse(packet + prefix.payload_offset,
            prefix.declared_payload_length, &frame, &event) != DF_OK ||
        frame.family != 0x07U || frame.opcode != 0x86U ||
        !df_gvs_frame_is_for_identity(&frame, identity) ||
        df_gvs_station_validate(frame.source) != DF_OK) return DF_ERR_INVALID;
    route = df_gvs_udp_preview_route_mutable(sender, frame.source);
    memcpy(route->peer, frame.source, sizeof(route->peer));
    route->ipv4 = prefix.source_ipv4;
    route->observed_ms = now_ms;
    route->configured = false;
    route->valid = true;
    return DF_OK;
}

int df_gvs_udp_sender_set_configured_route(struct df_gvs_udp_sender *sender,
    const uint8_t peer[6], uint32_t ipv4) {
    struct df_gvs_preview_route *route;

    if (sender == NULL || df_gvs_station_validate(peer) != DF_OK ||
        !df_gvs_udp_ipv4_is_unicast(ipv4)) return DF_ERR_INVALID;
    route = df_gvs_udp_preview_route_mutable(sender, peer);
    memcpy(route->peer, peer, sizeof(route->peer));
    route->ipv4 = ipv4;
    route->observed_ms = 0U;
    route->configured = true;
    route->valid = true;
    return DF_OK;
}

int df_gvs_udp_sender_resolve_preview_route(const struct df_gvs_udp_sender *sender,
    const uint8_t peer[6], uint64_t now_ms, uint64_t max_age_ms,
    uint32_t *ipv4) {
    size_t index;

    if (sender == NULL || df_gvs_station_validate(peer) != DF_OK ||
        ipv4 == NULL || max_age_ms == 0U) return DF_ERR_INVALID;
    for (index = 0U; index < DF_GVS_PREVIEW_ROUTE_CAPACITY; index++) {
        const struct df_gvs_preview_route *route = &sender->preview_routes[index];
        if (!route->valid || memcmp(route->peer, peer, 6U) != 0) continue;
        if (!route->configured && (now_ms < route->observed_ms ||
            now_ms - route->observed_ms >= max_age_ms)) return DF_ERR_INVALID;
        *ipv4 = route->ipv4;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

int df_gvs_udp_sender_emit_control(struct df_gvs_udp_sender *sender,
    const uint8_t destination[6], uint32_t destination_ipv4,
    const uint8_t source[6], uint8_t family, uint8_t opcode,
    const uint8_t *payload, size_t payload_length) {
    uint8_t frame[DF_GVS_CONTROL_HEADER_SIZE + UINT16_MAX];
    struct sockaddr_in target;
    size_t frame_length = 0U;
    ssize_t written;

    if (sender == NULL || sender->fd < 0 || destination == NULL ||
        !df_gvs_udp_ipv4_is_unicast(destination_ipv4) || source == NULL ||
        payload_length > UINT16_MAX ||
        (payload_length > 0U && payload == NULL) ||
        df_gvs_control_serialize(frame, sizeof(frame), &frame_length,
            destination, source, family, opcode, payload,
            (uint16_t)payload_length, sender->provide_fields,
            sender->fields_context) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_INVALID;
    }
    target = sender->peer;
    target.sin_addr.s_addr = destination_ipv4;
    written = sendto(sender->fd, frame, frame_length, 0,
        (const struct sockaddr *)&target, sizeof(target));
    if (written != (ssize_t)frame_length) {
        if (sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX) sender->sent++;
    return DF_OK;
}

int df_gvs_udp_sender_emit_station_scan(struct df_gvs_udp_sender *sender,
    const struct df_gvs_station_scan_action *action) {
    static const uint8_t payload[DF_GVS_STATION_SCAN_PAYLOAD_SIZE] = {
        0x02U, 0x00U, 0x00U, 0x01U};
    uint8_t frame[DF_GVS_CONTROL_HEADER_SIZE +
        DF_GVS_STATION_SCAN_PAYLOAD_SIZE];
    struct sockaddr_in target;
    size_t frame_length = 0U;
    ssize_t written;

    if (sender == NULL || sender->fd < 0 || action == NULL ||
        action->family != 0x07U || action->opcode != 0x06U ||
        action->payload_length != sizeof(payload) ||
        memcmp(action->payload, payload, sizeof(payload)) != 0 ||
        action->destination[0] != 0x32U ||
        action->destination[1] != action->source[1] ||
        action->destination[2] != action->source[2] ||
        memcmp(action->destination + 3U,
            (const uint8_t[]){0xffU, 0xffU, 0xffU}, 3U) != 0 ||
        df_gvs_control_serialize(frame, sizeof(frame), &frame_length,
            action->destination, action->source, action->family,
            action->opcode, action->payload, (uint16_t)action->payload_length,
            sender->provide_fields, sender->fields_context) != DF_OK) {
        if (sender != NULL && sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_INVALID;
    }
    target = sender->peer;
    target.sin_port = htons(8300U);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    written = sendto(sender->fd, frame, frame_length, 0,
        (const struct sockaddr *)&target, sizeof(target));
    if (written != (ssize_t)frame_length) {
        if (sender->failed < UINT_MAX) sender->failed++;
        return DF_ERR_IO;
    }
    if (sender->sent < UINT_MAX) sender->sent++;
    return DF_OK;
}

int df_gvs_udp_presence_emit(const struct df_gvs_presence_action *action,
    void *context) {
    struct df_gvs_udp_presence_context *ctx = context;
    uint8_t frame[DF_GVS_SYNC_MAX_PACKET_SIZE];
    size_t length = 0;
    char host[DF_GVS_IPV4_TEXT_SIZE];
    struct sockaddr_in destination;
    ssize_t written;
    if (action == NULL) {
        return DF_ERR_INVALID;
    }
    if (ctx != NULL) ctx->emitted_packets = 0U;
    /* Presence tracks peer transitions locally; they have no wire frame. */
    if (action->type == DF_GVS_PRESENCE_PEER_ONLINE ||
        action->type == DF_GVS_PRESENCE_PEER_OFFLINE) {
        return DF_OK;
    }
    if (ctx == NULL || ctx->sender == NULL || ctx->sender->fd < 0 ||
        ctx->source == NULL) {
        return DF_ERR_INVALID;
    }
    if (action->type == DF_GVS_PRESENCE_PERIODIC_SYNC) {
        size_t chunk_count;
        size_t chunk_index;

        if (ctx->store == NULL) return DF_OK;
        chunk_count = df_gvs_sync_periodic_chunk_count(ctx->store);
        for (chunk_index = 0U; chunk_index < chunk_count; chunk_index++) {
            if (df_gvs_sync_periodic_serialize(ctx->store, chunk_index, action,
                    ctx->source, ctx->sync_version, frame, sizeof(frame),
                    &length, ctx->sender->provide_fields,
                    ctx->sender->fields_context) != DF_OK ||
                df_gvs_udp_resolve_destination(ctx->sender, action->target,
                    &destination) != DF_OK) {
                if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
                return DF_ERR_INVALID;
            }
            written = sendto(ctx->sender->fd, frame, length, 0,
                (const struct sockaddr *)&destination, sizeof(destination));
            if (written != (ssize_t)length) {
                if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
                return DF_ERR_IO;
            }
            if (ctx->sender->sent < UINT_MAX) ctx->sender->sent++;
            ctx->emitted_packets++;
        }
        return DF_OK;
    }
    if (df_gvs_presence_action_serialize(action, ctx->source,
            ctx->sync_version, frame, sizeof(frame), &length,
            ctx->sender->provide_fields, ctx->sender->fields_context) != DF_OK ||
        df_gvs_identity_unicast_ip(action->target, host) != DF_OK) {
        if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
        return DF_ERR_INVALID;
    }
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = ctx->sender->peer.sin_port;
    if (inet_pton(AF_INET, host, &destination.sin_addr) != 1) {
        if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
        return DF_ERR_INVALID;
    }
    written = sendto(ctx->sender->fd, frame, length, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    if (written != (ssize_t)length) {
        if (ctx->sender->failed < UINT_MAX) ctx->sender->failed++;
        return DF_ERR_IO;
    }
    if (ctx->sender->sent < UINT_MAX) ctx->sender->sent++;
    ctx->emitted_packets = 1U;
    return DF_OK;
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
