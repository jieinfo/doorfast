#include "gvs_replay.h"

#include <pcap/pcap.h>
#include <stdbool.h>
#include <string.h>

#include "gvs_deadline.h"
#include "gvs_packet.h"
#include "gvs_receive.h"

static void df_gvs_replay_count_result(const struct df_gvs_receive_result *result,
                                       struct df_gvs_replay_stats *stats) {
    unsigned i;

    stats->accepted_calls += result->accepted_call ? 1U : 0U;
    stats->talking_transitions += result->talking_transition ? 1U : 0U;
    stats->rejected_pick_frames += result->rejected_pick ? 1U : 0U;
    stats->ended_transitions += result->ended_transition ? 1U : 0U;
    stats->observed_hangups += result->observed_hangup ? 1U : 0U;
    stats->preempted_sessions += result->preempted_session ? 1U : 0U;
    stats->time_sync_updates += result->time_sync_update ? 1U : 0U;
    stats->rejected_time_sync += result->rejected_time_sync ? 1U : 0U;
    stats->timed_out_sessions += result->timed_out_transition ? 1U : 0U;
    for (i = 0; i < result->transition.count; ++i) {
        if (result->transition.events[i].type == DF_EVENT_INCOMING_CALL) {
            stats->calls_started++;
        }
    }
}

int df_gvs_replay_file(const char *path, const uint8_t identity[6],
                       struct df_gvs_session *session,
                       struct df_gvs_replay_stats *stats) {
    char error_buffer[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *handle;
    const uint8_t *packet;
    struct pcap_pkthdr *header;
    int next_result;
    struct df_gvs_deadline deadline = {0};
    uint64_t last_ms = 0;
    bool have_time = false;

    if (path == NULL || path[0] == '\0' || identity == NULL || session == NULL || stats == NULL) {
        return DF_ERR_INVALID;
    }
    memset(stats, 0, sizeof(*stats));
    handle = pcap_open_offline(path, error_buffer);
    if (handle == NULL || pcap_datalink(handle) != DLT_EN10MB) {
        if (handle != NULL) {
            pcap_close(handle);
        }
        return DF_ERR_IO;
    }
    while ((next_result = pcap_next_ex(handle, &header, &packet)) >= 0) {
        const uint8_t *payload;
        size_t payload_length;
        struct df_gvs_receive_result result;
        int extracted;
        uint64_t now_ms;

        if (next_result == 0) {
            continue;
        }
        stats->packets_seen++;
        /* Offline time only: EOF is not a timeout, and time must not go backwards. */
        if (header->ts.tv_sec < 0 || header->ts.tv_usec < 0 ||
            header->ts.tv_usec >= 1000000 ||
            (uint64_t)header->ts.tv_sec > (UINT64_MAX - 999) / 1000) {
            stats->invalid_timestamps++;
            continue;
        }
        now_ms = (uint64_t)header->ts.tv_sec * 1000 +
                 (uint64_t)header->ts.tv_usec / 1000;
        if (have_time && now_ms < last_ms) {
            stats->invalid_timestamps++;
            continue;
        }
        have_time = true;
        last_ms = now_ms;
        {
            bool timed_out = false;
            if (df_gvs_deadline_tick(&deadline, session, now_ms, &timed_out) != DF_OK) {
                stats->invalid_timestamps++;
                continue;
            }
            if (timed_out) {
                stats->timed_out_sessions++;
            }
        }
        extracted = df_gvs_extract_control_payload(packet, header->caplen, &payload, &payload_length);
        if (extracted < 0) {
            stats->invalid_datagrams++;
            continue;
        }
        if (extracted == 0) {
            continue;
        }
        stats->control_datagrams++;
        if (df_gvs_receive_datagram(payload, payload_length, identity, session,
                                    &deadline, now_ms, &result) != DF_OK) {
            stats->invalid_datagrams++;
            continue;
        }
        df_gvs_replay_count_result(&result, stats);
    }
    pcap_close(handle);
    return next_result == -1 ? DF_ERR_IO : DF_OK;
}
