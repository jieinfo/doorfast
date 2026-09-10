#include "evidence_metadata.h"
#include "gvs_packet.h"
#include "gvs_frame.h"
#include <arpa/inet.h>

static void identity(char text[18], const uint8_t *bytes) {
    snprintf(text, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
}
int df_evidence_metadata_append(struct df_evidence_log *log,
    const struct df_capture_record *packet, const char *interface, uint64_t now) {
    struct df_udp_prefix prefix;
    struct df_gvs_frame frame;
    struct df_event event;
    const uint8_t *payload;
    size_t length;
    char source_mac[18], destination_mac[18], source_gvs[18], destination_gvs[18];
    char source_ip[INET_ADDRSTRLEN], destination_ip[INET_ADDRSTRLEN];
    if (!packet || !interface ||
        df_gvs_inspect_udp_prefix(packet->data, packet->captured_length, &prefix) != 1 ||
        df_gvs_extract_control_payload(packet->data, packet->captured_length,
            &payload, &length) != 1 ||
        df_gvs_frame_parse(payload, length, &frame, &event)) return DF_ERR_INVALID;
    size_t ip = packet->data[12] == 8 && packet->data[13] == 0 ? 14 : 18;
    identity(source_mac, packet->data + 6); identity(destination_mac, packet->data);
    identity(source_gvs, frame.source); identity(destination_gvs, frame.destination);
    if (!inet_ntop(AF_INET, packet->data + ip + 12, source_ip, sizeof(source_ip)) ||
        !inet_ntop(AF_INET, packet->data + ip + 16, destination_ip, sizeof(destination_ip)))
        return DF_ERR_INVALID;
    struct df_evidence_log_record record = {.wall_seconds = packet->wall_seconds,
        .wall_microseconds = packet->wall_microseconds, .monotonic_ms = now,
        .interface_name = interface, .source_mac = source_mac,
        .destination_mac = destination_mac, .source_ip = source_ip,
        .destination_ip = destination_ip, .source_port = prefix.source_port,
        .destination_port = prefix.destination_port, .source_gvs = source_gvs,
        .destination_gvs = destination_gvs, .family = frame.family,
        .opcode = frame.opcode, .length = (uint32_t)length,
        /* Zero denotes uncorrelated capture; this process owns no call session. */
        .session_generation = 0, .health_state = "recording"};
    return df_evidence_log_append(log, &record);
}
