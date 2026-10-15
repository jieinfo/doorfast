/* Standalone diagnostic only: never linked into or installed with Doorfast. */
#include "time_query_probe.h"
#include "runtime_id.h"
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <pcap/pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef DF_WITH_UBUS
#include <libubox/blobmsg.h>
#include <libubus.h>
#endif

static volatile sig_atomic_t interrupted;
static void stop(int signal_number) { (void)signal_number; interrupted=1; }
static uint64_t milliseconds(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock,&t)!=0) return 0;
    return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;
}

#ifdef DF_WITH_UBUS
struct reader { struct ubus_context *ubus; uint32_t id; struct tq_owner owner; bool valid; };
static bool text(struct blob_attr *a,char *output,size_t capacity) {
    const char *s;
    if (a==NULL || blobmsg_type(a)!=BLOBMSG_TYPE_STRING) return false;
    s=blobmsg_get_string(a);
    if (strlen(s)>=capacity) return false;
    memcpy(output,s,strlen(s)+1); return true;
}
static void status_reply(struct ubus_request *r,int type,struct blob_attr *message) {
    const struct blobmsg_policy root_policy[]={
        {.name="runtime_id",.type=BLOBMSG_TYPE_STRING}, {.name="station_call",.type=BLOBMSG_TYPE_TABLE},
        {.name="call",.type=BLOBMSG_TYPE_TABLE}, {.name="media",.type=BLOBMSG_TYPE_TABLE}};
    const struct blobmsg_policy op_policy[]={
        {.name="station_id",.type=BLOBMSG_TYPE_STRING}, {.name="operation_id",.type=BLOBMSG_TYPE_INT64},
        {.name="stage",.type=BLOBMSG_TYPE_STRING}, {.name="call_generation",.type=BLOBMSG_TYPE_INT64}};
    const struct blobmsg_policy call_policy[]={
        {.name="station_id",.type=BLOBMSG_TYPE_STRING}, {.name="session",.type=BLOBMSG_TYPE_STRING},
        {.name="generation",.type=BLOBMSG_TYPE_INT64}};
    const struct blobmsg_policy media_policy[]={ {.name="sessions",.type=BLOBMSG_TYPE_ARRAY} };
    struct blob_attr *root[4]={0},*op[4]={0},*call[3]={0},*media[1]={0};
    struct reader *reader=r->priv;
    struct tq_owner s={0};
    (void)type;
    if (!message || blobmsg_parse(root_policy,4,root,blob_data(message),blob_len(message))!=0) return;
    for(unsigned i=0;i<4;i++) if(!root[i]) return;
    if (blobmsg_parse(op_policy,4,op,blobmsg_data(root[1]),blobmsg_len(root[1]))!=0 ||
        blobmsg_parse(call_policy,3,call,blobmsg_data(root[2]),blobmsg_len(root[2]))!=0 ||
        blobmsg_parse(media_policy,1,media,blobmsg_data(root[3]),blobmsg_len(root[3]))!=0) return;
    for(unsigned i=0;i<4;i++) if(!op[i]) return;
    for(unsigned i=0;i<3;i++) if(!call[i]) return;
    if (!media[0] || !text(root[0],s.runtime,sizeof(s.runtime)) || !df_runtime_id_is_valid(s.runtime) ||
        !text(op[0],s.station,sizeof(s.station)) || !text(op[2],s.stage,sizeof(s.stage)) ||
        !text(call[0],s.native_station,sizeof(s.native_station)) || !text(call[1],s.native_state,sizeof(s.native_state))) return;
    s.operation=blobmsg_get_u64(op[1]); s.generation=blobmsg_get_u64(op[3]);
    s.native_generation=blobmsg_get_u64(call[2]); s.media_empty=blobmsg_len(media[0])==0;
    reader->owner=s; reader->valid=true;
}
static int read_owner(struct reader *r,struct tq_owner *s) {
    r->valid=false;
    if (ubus_invoke(r->ubus,r->id,"status",NULL,status_reply,r,20)!=0 || !r->valid) return -1;
    *s=r->owner; return 0;
}
#else
struct reader { int unused; };
static int read_owner(struct reader *r,struct tq_owner *s) { (void)r; (void)s; return -1; }
#endif

static int logical(const char *s,uint8_t output[6]) {
    if (strlen(s)!=12) return -1;
    for(unsigned i=0;i<6;i++) {
        char pair[3]={s[i*2],s[i*2+1],0},*end;
        unsigned long n;
        if (strspn(pair,"0123456789abcdefABCDEF")!=2) return -1;
        n=strtoul(pair,&end,16); if(*end) return -1; output[i]=(uint8_t)n;
    }
    return 0;
}
static int arguments(int argc,char **argv,struct tq_config *c,const char **interface) {
    unsigned seen=0;
    memset(c,0,sizeof(*c)); *interface=NULL;
    for(int i=1;i<argc;i++) {
        const char *key=argv[i],*value;
        unsigned bit;
        if(strcmp(key,"--send-vendor-time-query")==0) { if(c->send)return -1; c->send=true; continue; }
        if(i+1>=argc)return -1;
        value=argv[++i];
        if(strcmp(key,"--interface")==0) { bit=1; if(!*value || strlen(value)>15)return -1; *interface=value; }
        else if(strcmp(key,"--runtime-id")==0) { bit=2; if(!df_runtime_id_is_valid(value))return -1; memcpy(c->runtime,value,17); }
        else if(strcmp(key,"--station-id")==0) {
            bit=4; if(!*value || strlen(value)>=sizeof(c->station) || strspn(value,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(value))return -1;
            strcpy(c->station,value);
        } else if(strcmp(key,"--operation-id")==0) {
            char *end; bit=8; errno=0;
            if(!*value || strspn(value,"0123456789")!=strlen(value))return -1;
            c->operation=strtoull(value,&end,10); if(errno || *end || !c->operation)return -1;
        } else if(strcmp(key,"--local-ip")==0) { bit=16; if(inet_pton(AF_INET,value,&c->local_ip)!=1)return -1; }
        else if(strcmp(key,"--peer-ip")==0) { bit=32; if(inet_pton(AF_INET,value,&c->peer_ip)!=1)return -1; }
        else if(strcmp(key,"--local-address")==0) { bit=64; if(logical(value,c->local)!=0)return -1; }
        else if(strcmp(key,"--peer-address")==0) { bit=128; if(logical(value,c->peer)!=0)return -1; }
        else return -1;
        if(seen&bit)return -1;
        seen|=bit;
    }
    return seen==255 && c->local_ip!=c->peer_ip ? 0 : -1;
}
struct transport { pcap_t *pcap; struct tq_probe *probe; };
static int inject(const uint8_t *b,size_t n,void *context) {
    struct transport *t=context;
    uint64_t now=milliseconds(CLOCK_MONOTONIC);
    /* Recheck after random generation/serialization, immediately before IO. */
    if (interrupted || now<t->probe->ack_ms || now-t->probe->ack_ms>100) return -1;
    return pcap_inject(t->pcap,b,n)==(int)n ? 0 : -1;
}
static bool idle(const struct tq_owner *s,const struct tq_config *c) {
    return strcmp(s->runtime,c->runtime)==0 && s->operation<c->operation &&
        s->operation+1==c->operation && s->media_empty &&
        (strcmp(s->native_state,"idle")==0 || strcmp(s->native_state,"ended")==0) &&
        (strcmp(s->stage,"idle")==0 || strcmp(s->stage,"ended")==0 || strcmp(s->stage,"failed")==0);
}
int main(int argc,char **argv) {
    struct tq_config config;
    struct tq_probe probe;
    struct tq_owner owner;
    struct reader reader={0};
    struct bpf_program filter;
    pcap_t *pcap=NULL;
    const char *interface;
    char error[PCAP_ERRBUF_SIZE],expression[512],local[INET_ADDRSTRLEN],peer[INET_ADDRSTRLEN];
    int result=1;
    uint64_t start,next_owner_check;
    if(argc==2 && strcmp(argv[1],"--help")==0) {
        puts("usage: doorfast-time-query-probe --interface IFACE --runtime-id HEX16 --station-id ID --operation-id NEXT "
             "--local-ip IPv4 --peer-ip IPv4 --local-address HEX12 --peer-address HEX12 [--send-vendor-time-query]\n"
             "Default is observe-only. Fixed ports 8300/8303/8302, max 35s, one call, at most two vendor-short 03/56 queries within ACK+100ms.");
        return 0;
    }
    if(arguments(argc,argv,&config,&interface)!=0) { fputs("invalid_arguments\n",stderr); return 2; }
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGALRM,stop); alarm(35);
#ifdef DF_WITH_UBUS
    reader.ubus=ubus_connect(NULL);
    if(!reader.ubus || ubus_lookup_id(reader.ubus,"doorfast",&reader.id)!=0) goto done;
#endif
    if(read_owner(&reader,&owner)!=0 || !idle(&owner,&config)) { fputs("preflight_owner_unavailable_or_not_idle\n",stderr); goto done; }
    pcap=pcap_create(interface,error);
    if(!pcap || pcap_set_snaplen(pcap,128)!=0 || pcap_set_promisc(pcap,0)!=0 ||
        pcap_set_timeout(pcap,1)!=0 || pcap_set_immediate_mode(pcap,1)!=0 || pcap_activate(pcap)!=0 ||
        pcap_datalink(pcap)!=DLT_EN10MB || pcap_setnonblock(pcap,1,error)!=0) goto done;
    if(!inet_ntop(AF_INET,&config.local_ip,local,sizeof(local)) || !inet_ntop(AF_INET,&config.peer_ip,peer,sizeof(peer))) goto done;
    (void)snprintf(expression,sizeof(expression),"udp src port 8300 and udp dst port 8300 and host %s and udp[46] = 3 and ((host %s and (udp[47] = 2 or udp[47] = 3 or udp[47] = 131)) or (dst host %s and udp[47] = 1))",local,peer,local);
    if(pcap_compile(pcap,&filter,expression,1,PCAP_NETMASK_UNKNOWN)!=0) goto done;
    if(pcap_setfilter(pcap,&filter)!=0) { pcap_freecode(&filter); goto done; }
    pcap_freecode(&filter);
    tq_init(&probe,&config); start=milliseconds(CLOCK_MONOTONIC); next_owner_check=start;
    printf("{\"event\":\"ready\",\"send_enabled\":%s,\"operation_id\":%"PRIu64"}\n",config.send?"true":"false",config.operation); fflush(stdout);
    while(!interrupted && milliseconds(CLOCK_MONOTONIC)-start<35000) {
        struct pcap_pkthdr *header;
        const u_char *bytes;
        int got;
        if(milliseconds(CLOCK_MONOTONIC)>=next_owner_check) {
            if(read_owner(&reader,&owner)!=0 || !tq_check_owner(&probe,&owner)) { probe.cancelled=true; break; }
            next_owner_check=milliseconds(CLOCK_MONOTONIC)+50;
        }
        got=pcap_next_ex(pcap,&header,&bytes);
        if(got<0) break;
        if(got==1) {
            uint64_t wall=milliseconds(CLOCK_REALTIME),stamp=(uint64_t)header->ts.tv_sec*1000+(uint64_t)header->ts.tv_usec/1000;
            if(wall>=stamp && wall-stamp<=100) {
                uint64_t captured_at=milliseconds(CLOCK_MONOTONIC)-(wall-stamp);
                if(read_owner(&reader,&owner)!=0 || !tq_check_owner(&probe,&owner)) { probe.cancelled=true; break; }
                uint64_t now=milliseconds(CLOCK_MONOTONIC);
                if(now>=captured_at && now-captured_at<=100)
                    (void)tq_observe(&probe,bytes,header->caplen,now,(unsigned)(now-captured_at),&owner);
            }
        }
        if(probe.cancelled) break;
        if(probe.ack_seen) {
            struct transport transport={pcap,&probe};
            if(!config.send) { result=0; break; }
            while(!interrupted && probe.attempts<2 && milliseconds(CLOCK_MONOTONIC)-probe.ack_ms<=100) {
                if(read_owner(&reader,&owner)!=0 || !tq_check_owner(&probe,&owner)) { probe.cancelled=true; break; }
                uint64_t now=milliseconds(CLOCK_MONOTONIC);
                if(tq_may_send(&probe,&owner,now)) {
                    if(tq_send(&probe,&owner,now,inject,&transport)!=0) break;
                } else if(strcmp(owner.stage,"calling")!=0) break;
                usleep(2000);
            }
            result=probe.sent==2 ? 0 : 1; break;
        }
        usleep(2000);
    }
    printf("{\"event\":\"finished\",\"call_seen\":%s,\"ack_seen\":%s,\"cancelled\":%s,\"attempts\":%u,\"sent\":%u,\"valid_treatment\":%s}\n",
        probe.call_seen?"true":"false",probe.ack_seen?"true":"false",probe.cancelled?"true":"false",probe.attempts,probe.sent,
        config.send && result==0?"true":"false");
done:
    if(pcap) pcap_close(pcap);
#ifdef DF_WITH_UBUS
    if(reader.ubus) ubus_free(reader.ubus);
#endif
    alarm(0); return result;
}
