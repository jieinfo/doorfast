#include <pcap/pcap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gvs_identity.h"
#include "gvs_replay.h"
#include "test.h"

/* Synthetic offline fixtures only: no device-valid authentication or transmission. */
static void dump_preemption_packet(pcap_dumper_t *dumper, int second,
    const uint8_t source[6], uint8_t opcode, uint8_t payload_length) {
    uint8_t packet[99] = {0};
    struct pcap_pkthdr header = {.caplen = 84 + payload_length,
        .len = 84 + payload_length};
    header.ts.tv_sec = second;
    packet[12] = 8;
    packet[14] = 0x45;
    packet[17] = 70 + payload_length;
    packet[23] = 17;
    packet[34] = packet[36] = 0x20;
    packet[35] = packet[37] = 0x6c;
    packet[39] = 50 + payload_length;
    memcpy(packet + 42, "GVSGVS\xa5\xa5\xa5\xa5", 10);
    memcpy(packet + 52, (uint8_t[]){0x61, 2, 1, 1, 1, 1}, 6);
    memcpy(packet + 58, source, 6);
    packet[80] = 3;
    packet[81] = opcode;
    packet[82] = payload_length;
    pcap_dump((u_char *)dumper, &header, packet);
}

void test_gvs_replay_preemption_deadline(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 1};
    const uint8_t old[6] = {0x61, 2, 1, 1, 1, 2};
    const uint8_t next[6] = {0x32, 2, 1, 0, 1, 0};
    char path[] = "/tmp/doorfast-preemption-XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT_INT_EQ(1, fd >= 0);
    if (fd < 0) return;
    close(fd);
    for (int mode = 0; mode < 4; ++mode) {
        pcap_t *dead = pcap_open_dead(DLT_EN10MB, 2048);
        TEST_ASSERT_INT_EQ(1, dead != NULL);
        if (dead == NULL) break;
        pcap_dumper_t *dumper = pcap_dump_open(dead, path);
        TEST_ASSERT_INT_EQ(1, dumper != NULL);
        if (dumper == NULL) { pcap_close(dead); break; }
        dump_preemption_packet(dumper, 0, old, 1, 15);
        dump_preemption_packet(dumper, 29, next, 1, 15);
        dump_preemption_packet(dumper, 30, old, 2, 1);
        dump_preemption_packet(dumper, 31, old, 0x83, 7);
        if (mode == 2) dump_preemption_packet(dumper, 58, next, 1, 15);
        if (mode == 3) dump_preemption_packet(dumper, 58, old, 1, 15);
        dump_preemption_packet(dumper, mode == 0 ? 58 : 59, next, 0xff, 0);
        pcap_dump_close(dumper);
        pcap_close(dead);
        struct df_gvs_session session = {0};
        struct df_gvs_replay_stats stats;
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, local, &session, &stats));
        TEST_ASSERT_INT_EQ(2, stats.calls_started);
        TEST_ASSERT_INT_EQ(1, stats.ended_transitions);
        TEST_ASSERT_INT_EQ(0, stats.observed_hangups);
        TEST_ASSERT_INT_EQ(1, stats.preempted_sessions);
        TEST_ASSERT_INT_EQ(stats.ended_transitions,
            stats.observed_hangups + stats.preempted_sessions);
        TEST_ASSERT_INT_EQ(1, stats.rejected_pick_frames);
        TEST_ASSERT_INT_EQ(mode == 0 ? 0 : 1, stats.timed_out_sessions);
        TEST_ASSERT_INT_EQ(mode == 0 ? DF_GVS_RINGING : DF_GVS_ENDED, session.state);
        TEST_ASSERT_INT_EQ(0, memcmp(session.peer, next, 6));
        TEST_ASSERT_INT_EQ(2, session.generation);
        TEST_ASSERT_INT_EQ(mode == 2 ? 3 : 2, stats.accepted_calls);
    }
    remove(path);
}

static void write_control_pcap(const char *path, int reply_second, int wrong_peer) {
    uint8_t packet[99] = {0};
    static const uint8_t gvs_header[] = {'G', 'V', 'S', 'G', 'V', 'S',
                                         0xa5, 0xa5, 0xa5, 0xa5};
    pcap_t *dead = pcap_open_dead(DLT_EN10MB, 2048);
    pcap_dumper_t *dumper;
    struct pcap_pkthdr header = {.caplen = 99, .len = 99};

    TEST_ASSERT_INT_EQ(1, dead != NULL);
    if (dead == NULL) {
        return;
    }
    dumper = pcap_dump_open(dead, path);
    TEST_ASSERT_INT_EQ(1, dumper != NULL);
    if (dumper == NULL) {
        pcap_close(dead);
        return;
    }
    packet[12] = 0x08;
    packet[13] = 0x00;
    packet[14] = 0x45;
    packet[16] = 0x00;
    packet[17] = 85;
    packet[23] = 17;
    packet[34] = 0x20;
    packet[35] = 0x6c;
    packet[36] = 0x20;
    packet[37] = 0x6c;
    packet[38] = 0x00;
    packet[39] = 65;
    memcpy(packet + 42, gvs_header, sizeof(gvs_header));
    memcpy(packet + 52, (const uint8_t[]){0x61, 0x02, 0x01, 0x01, 0x01, 0x00}, 6);
    memcpy(packet + 58, (const uint8_t[]){0x32, 0x02, 0x01, 0x00, 0x01, 0x00}, 6);
    packet[80] = 0x03;
    packet[81] = 0x01;
    packet[82] = 15;
    memcpy(packet + 84, (const uint8_t[]){0, 0x20, 0x6f, 0, 0, 0, 30, 0, 1, 0, 0, 0, 0, 0, 0}, 15);
    pcap_dump((u_char *)dumper, &header, packet);
    if (reply_second > 0) {
        header.caplen = header.len = 91;
        header.ts.tv_sec = 1;
        packet[17] = 77;
        packet[39] = 57;
        packet[82] = 7;
        memcpy(packet + 52, (const uint8_t[]){0x32, 2, 1, 0, 1, 0}, 6);
        memcpy(packet + 58, (const uint8_t[]){0x61, 2, 1, 1, 1, 1}, 6);
        packet[81] = 3;
        memcpy(packet + 84, (const uint8_t[]){2, 0x20, 0x6f, 0, 0x20, 0x6e, 120}, 7);
        pcap_dump((u_char *)dumper, &header, packet);
        header.ts.tv_sec = reply_second;
        memcpy(packet + 52, (const uint8_t[]){0x61, 2, 1, 1, 1, 1}, 6);
        memcpy(packet + 58, (const uint8_t[]){0x32, 2, 1, 0, 1, 0}, 6);
        if (wrong_peer == 1) packet[62] = 2;
        packet[81] = 0x83;
        memcpy(packet + 84, (const uint8_t[]){0, 0x20, 0x6f, 0, 0x20, 0x6e, 30}, 7);
        pcap_dump((u_char *)dumper, &header, packet);
        if (wrong_peer == 2) {
            /* Synthetic remote hangup, then a second call and retransmission. */
            header.ts.tv_sec = 5;
            header.caplen = header.len = 85;
            packet[17] = 71;
            packet[39] = 51;
            packet[82] = 1;
            packet[81] = 2;
            packet[84] = 1;
            pcap_dump((u_char *)dumper, &header, packet);
            header.ts.tv_sec = 6;
            header.caplen = header.len = 99;
            packet[17] = 85;
            packet[39] = 65;
            packet[82] = 15;
            packet[81] = 1;
            memcpy(packet + 84, (const uint8_t[]){0, 0x20, 0x6f, 0, 0, 0, 30, 0, 1, 0, 0, 0, 0, 0, 0}, 15);
            pcap_dump((u_char *)dumper, &header, packet);
            header.ts.tv_usec = 1000;
            pcap_dump((u_char *)dumper, &header, packet);
        }
    }
    if (wrong_peer >= 4 && wrong_peer <= 7) {
        header.ts.tv_sec = 3;
        header.caplen = header.len = 85;
        packet[17] = 71;
        packet[39] = 51;
        packet[82] = 1;
        packet[81] = 0x57;
        packet[84] = 1; /* one second remaining */
        if (wrong_peer == 6) packet[84] = 0;
        if (wrong_peer == 7) packet[84] = 255;
        if (wrong_peer == 5) packet[62] = 9;
        pcap_dump((u_char *)dumper, &header, packet);
    }
    if (wrong_peer >= 3) {
        /* No hangup: next call arrives at the policy timeout boundary. */
        header.ts.tv_sec = reply_second > 0 ? 122 : 30;
        if (wrong_peer >= 4) header.ts.tv_sec = 4;
        header.ts.tv_usec = 0;
        header.caplen = header.len = 99;
        packet[17] = 85;
        packet[39] = 65;
        packet[82] = 15;
        packet[81] = 1;
        memcpy(packet + 52, (const uint8_t[]){0x61, 2, 1, 1, 1, 1}, 6);
        memcpy(packet + 58, (const uint8_t[]){0x32, 2, 1, 0, 2, 0}, 6);
        pcap_dump((u_char *)dumper, &header, packet);
    }
    pcap_dump_close(dumper);
    pcap_close(dead);
}

void test_gvs_replay_reads_an_offline_control_packet_without_transmitting(void) {
    char path[128];
    uint8_t identity[6];
    struct df_gvs_session session = {0};
    struct df_gvs_replay_stats stats = {0};

    (void)snprintf(path, sizeof(path), "/tmp/doorfast-gvs-%ld.pcap", (long)getpid());
    write_control_pcap(path, 0, 0);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_identity_parse("IS:2-1-101-1", identity));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, identity, &session, &stats));
    TEST_ASSERT_INT_EQ(1, (int)stats.packets_seen);
    TEST_ASSERT_INT_EQ(1, (int)stats.control_datagrams);
    TEST_ASSERT_INT_EQ(1, (int)stats.accepted_calls);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    TEST_ASSERT_INT_EQ(0, (int)stats.timed_out_sessions); /* EOF does not invent elapsed time. */
    for (int scenario = 0; scenario < 3; scenario++) {
        memset(&session, 0, sizeof(session));
        write_control_pcap(path, scenario == 1 ? 4 : 2, scenario == 2);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, identity, &session, &stats));
        TEST_ASSERT_INT_EQ(3, (int)stats.control_datagrams);
        TEST_ASSERT_INT_EQ(1, (int)stats.accepted_calls);
        TEST_ASSERT_INT_EQ(scenario == 0 ? 1 : 0, (int)stats.talking_transitions);
        TEST_ASSERT_INT_EQ(scenario == 0 ? 0 : 1, (int)stats.rejected_pick_frames);
        TEST_ASSERT_INT_EQ(scenario == 0 ? DF_GVS_TALKING : DF_GVS_RINGING, session.state);
    }
    memset(&session, 0, sizeof(session));
    write_control_pcap(path, 2, 2);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, identity, &session, &stats));
    TEST_ASSERT_INT_EQ(2, (int)stats.calls_started);
    TEST_ASSERT_INT_EQ(3, (int)stats.accepted_calls);
    TEST_ASSERT_INT_EQ(1, (int)stats.talking_transitions);
    TEST_ASSERT_INT_EQ(1, (int)stats.ended_transitions);
    TEST_ASSERT_INT_EQ(1, (int)stats.observed_hangups);
    TEST_ASSERT_INT_EQ(0, (int)stats.preempted_sessions);
    TEST_ASSERT_INT_EQ((int)stats.ended_transitions,
        (int)(stats.observed_hangups + stats.preempted_sessions));
    TEST_ASSERT_INT_EQ(2, (int)session.generation);
    TEST_ASSERT_INT_EQ(0, (int)session.pick_generation);
    TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
    for (int answered = 0; answered < 2; answered++) {
        memset(&session, 0, sizeof(session));
        write_control_pcap(path, answered ? 2 : 0, 3);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, identity, &session, &stats));
        TEST_ASSERT_INT_EQ(1, (int)stats.timed_out_sessions);
        TEST_ASSERT_INT_EQ(2, (int)stats.calls_started);
        TEST_ASSERT_INT_EQ(DF_GVS_RINGING, session.state);
        TEST_ASSERT_INT_EQ(2, session.peer[4]);
        TEST_ASSERT_INT_EQ(0, (int)session.pick_generation);
    }
    for (int mode = 4; mode <= 7; mode++) {
        int valid_sync = mode == 4 || mode == 6;
        memset(&session, 0, sizeof(session));
        write_control_pcap(path, 2, mode);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_replay_file(path, identity, &session, &stats));
        TEST_ASSERT_INT_EQ(valid_sync ? 1 : 0, (int)stats.time_sync_updates);
        TEST_ASSERT_INT_EQ(valid_sync ? 0 : 1, (int)stats.rejected_time_sync);
        TEST_ASSERT_INT_EQ(valid_sync ? 1 : 0, (int)stats.timed_out_sessions);
        TEST_ASSERT_INT_EQ(valid_sync ? 2 : 1, (int)stats.calls_started);
    }
    (void)remove(path);
}

void test_gvs_replay_handshake_gap_and_eof(void) {
    char path[] = "/tmp/doorfast-hand-replay-XXXXXX";
    const uint8_t local[6]={0x61,2,1,1,1,1};
    const uint8_t peer[6]={0x32,2,1,0,1,0};
    int fd=mkstemp(path);
    TEST_ASSERT_INT_EQ(1,fd>=0);
    if(fd<0)return;
    close(fd);
    for(int gap=0;gap<2;++gap) {
        pcap_t *dead=pcap_open_dead(DLT_EN10MB,2048);
        pcap_dumper_t *dump=pcap_dump_open(dead,path);
        TEST_ASSERT_INT_EQ(1,dump!=NULL);
        if(dump==NULL){pcap_close(dead);break;}
        dump_preemption_packet(dump,0,peer,1,15);
        if(gap)dump_preemption_packet(dump,11,peer,0xff,0);
        pcap_dump_close(dump);pcap_close(dead);
        struct df_gvs_session session={0};
        struct df_gvs_replay_stats stats;
        TEST_ASSERT_INT_EQ(DF_OK,df_gvs_replay_handshake_file(path,local,&session,&stats));
        TEST_ASSERT_INT_EQ(gap?5:1,stats.simulated_frames);
        TEST_ASSERT_INT_EQ(gap,stats.simulated_disconnects);
        TEST_ASSERT_INT_EQ(gap?DF_GVS_ENDED:DF_GVS_RINGING,session.state);
    }
    remove(path);
}
