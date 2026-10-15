#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include "../tools/diagnostics/time_query_probe.h"
#include "gvs_frame.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); return 1; } } while (0)

static struct tq_config config(void) {
    struct tq_config c = {.runtime="1111111111111111", .station="gate_main", .operation=7, .send=true,
        .local={0x61,2,1,0x19,1,1}, .peer={0x32,2,1,0,1,0}};
    (void)inet_pton(AF_INET, "10.5.83.0", &c.local_ip);
    (void)inet_pton(AF_INET, "10.5.64.0", &c.peer_ip);
    return c;
}
static struct tq_owner owner(void) {
    return (struct tq_owner){.runtime="1111111111111111", .station="gate_main", .operation=7,
        .stage="talking", .generation=42, .native_station="gate_main", .native_state="talking", .native_generation=42};
}
static size_t packet(uint8_t b[128], unsigned opcode) {
    struct tq_config c = config();
    bool incoming = opcode != 3;
    const uint8_t magic[10]={'G','V','S','G','V','S',0xa5,0xa5,0xa5,0xa5};
    unsigned payload = opcode == 2 ? 1 : 7;
    memset(b, 0, 128);
    b[0]=2; b[6]=4; b[12]=8; b[14]=0x45; b[16]=0; b[17]=(uint8_t)(70+payload); b[22]=64; b[23]=17;
    memcpy(b+26, incoming ? &c.peer_ip : &c.local_ip, 4);
    memcpy(b+30, incoming ? &c.local_ip : &c.peer_ip, 4);
    b[34]=0x20; b[35]=0x6c; b[36]=0x20; b[37]=0x6c; b[39]=(uint8_t)(50+payload);
    memcpy(b+42, magic, 10);
    memcpy(b+52, incoming ? c.local : c.peer, 6); memcpy(b+58, incoming ? c.peer : c.local, 6);
    b[80]=3; b[81]=(uint8_t)opcode; b[82]=(uint8_t)payload;
    if (payload==7) { b[84]=incoming?0:2; b[85]=0x20; b[86]=0x6f; b[87]=0; b[88]=0x20; b[89]=0x6e; b[90]=incoming?30:120; }
    return 84+payload;
}
static int prepare(struct tq_probe *p) {
    uint8_t b[128]; struct tq_config c=config(); struct tq_owner s=owner();
    tq_init(p,&c);
    if (tq_observe(p,b,packet(b,3),1000,0,&s)!=1) return -1;
    return tq_observe(p,b,packet(b,0x83),1300,2,&s)==1 ? 0 : -1;
}
struct sink { uint8_t frame[128]; size_t length; unsigned calls; bool fail; };
static int emit(const uint8_t *b,size_t n,void *v) {
    struct sink *s=v; s->calls++; memcpy(s->frame,b,n); s->length=n; return s->fail?-1:0;
}
static int serializes_only_vendor_short_query(void) {
    struct tq_probe p; struct tq_owner s=owner(); struct sink out={0}; struct df_gvs_frame f; struct df_event e;
    CHECK(prepare(&p)==0); CHECK(tq_may_send(&p,&s,1301)); CHECK(tq_send(&p,&s,1301,emit,&out)==0);
    CHECK(out.calls==1 && out.length==84); CHECK(out.frame[17]==70 && out.frame[39]==50);
    CHECK(out.frame[80]==3 && out.frame[81]==0x56 && out.frame[82]==1 && out.frame[83]==0);
    CHECK(memcmp(out.frame+52,config().peer,6)==0); CHECK(memcmp(out.frame+58,config().local,6)==0);
    {
        unsigned sum=0;
        const unsigned mask[8]={0,0x42,0x42,0x41,0x26,0x53,0x56,0x47};
        for(unsigned i=14;i<34;i+=2)sum+=((unsigned)out.frame[i]<<8)|out.frame[i+1];
        while(sum>>16)sum=(sum&0xffff)+(sum>>16);
        CHECK(sum==0xffff);
        for(unsigned i=0;i<8;i++)CHECK(out.frame[72+i]==(uint8_t)((out.frame[64+i]<<1)^mask[i]));
    }
    CHECK(df_gvs_frame_parse(out.frame+42,42,&f,&e)!=0); /* Product parser stays strict. */
    return 0;
}
static int owner_changes_never_send(void) {
    struct tq_probe p; struct tq_owner s; struct sink out={0};
    CHECK(prepare(&p)==0);
    for (unsigned i=0;i<7;i++) {
        s=owner();
        switch(i) { case 0:s.runtime[0]='2';break; case 1:s.station[0]='x';break;
        case 2:s.operation++;break;case 3:s.generation++;break;case 4:s.native_generation++;break;
        case 5:strcpy(s.stage,"ended");break;case 6:strcpy(s.native_state,"ringing");strcpy(s.native_station,"other");break; }
        CHECK(!tq_may_send(&p,&s,1301)); CHECK(tq_send(&p,&s,1301,emit,&out)!=0);
    }
    CHECK(out.calls==0); return 0;
}
static int ack_order_age_and_duplicates(void) {
    struct tq_probe p; struct tq_config c=config(); struct tq_owner s=owner(); uint8_t b[128];
    tq_init(&p,&c); CHECK(tq_observe(&p,b,packet(b,0x83),1000,0,&s)==0); CHECK(!p.ack_seen);
    CHECK(tq_observe(&p,b,packet(b,3),1000,0,&s)==1);
    CHECK(tq_observe(&p,b,packet(b,0x83),1300,101,&s)==0); CHECK(!p.ack_seen);
    CHECK(tq_observe(&p,b,packet(b,0x83),1300,2,&s)==1);
    CHECK(tq_observe(&p,b,packet(b,0x83),1350,0,&s)==0); CHECK(p.ack_ms==1298);
    CHECK(!tq_may_send(&p,&s,1399)); CHECK(!tq_may_send(&p,&s,1297)); return 0;
}
static int wrong_ack_and_fragment_rejected(void) {
    struct tq_probe p; struct tq_config c=config(); struct tq_owner s=owner(); uint8_t b[128]; size_t n;
    tq_init(&p,&c); CHECK(tq_observe(&p,b,packet(b,3),1000,0,&s)==1);
    for(unsigned i=0;i<6;i++) {
        n=packet(b,0x83);
        if(i==0)b[52]^=1; if(i==1)b[26]^=1; if(i==2)b[86]^=1; if(i==3)b[35]^=1; if(i==4)b[20]=0x20; if(i==5)b[90]=0;
        CHECK(tq_observe(&p,b,n,1300,0,&s)==0); CHECK(!p.ack_seen);
    }
    return 0;
}
static int hangup_cancels_query(void) {
    struct tq_probe p; struct tq_owner s=owner(); uint8_t b[128]; struct sink out={0};
    CHECK(prepare(&p)==0); CHECK(tq_observe(&p,b,packet(b,2),1301,0,&s)==1);
    CHECK(p.cancelled); CHECK(tq_send(&p,&s,1302,emit,&out)!=0); CHECK(out.calls==0); return 0;
}
static int default_off_and_bounded_attempts(void) {
    struct tq_probe p; struct tq_owner s=owner(); struct sink out={.fail=true};
    CHECK(prepare(&p)==0); p.config.send=false; CHECK(tq_send(&p,&s,1301,emit,&out)!=0); CHECK(out.calls==0);
    p.config.send=true;
    CHECK(tq_send(&p,&s,1301,emit,&out)!=0); CHECK(tq_send(&p,&s,1302,emit,&out)!=0);
    CHECK(tq_send(&p,&s,1303,emit,&out)!=0); CHECK(out.calls==2 && p.attempts==2 && p.sent==0); return 0;
}
static int conflicts_and_unrelated_calls_latch_cancel(void) {
    struct tq_probe p; struct tq_owner s=owner(),bad; struct tq_config c=config(); uint8_t b[128]; struct sink out={0};
    CHECK(prepare(&p)==0); bad=s; bad.operation++;
    CHECK(!tq_check_owner(&p,&bad)); CHECK(p.cancelled);
    CHECK(!tq_check_owner(&p,&s)); CHECK(tq_send(&p,&s,1301,emit,&out)!=0); CHECK(out.calls==0);
    tq_init(&p,&c); bad=s; bad.operation--; strcpy(bad.stage,"ended"); strcpy(bad.native_state,"ended"); bad.media_empty=true;
    CHECK(tq_check_owner(&p,&bad));
    CHECK(tq_observe(&p,b,packet(b,3),1000,0,&s)==1);
    bad=s; strcpy(bad.runtime,"2222222222222222");
    CHECK(tq_observe(&p,b,packet(b,0x83),1300,0,&bad)==0); CHECK(p.cancelled);
    CHECK(tq_observe(&p,b,packet(b,0x83),1301,0,&s)==0); CHECK(!p.ack_seen);
    tq_init(&p,&c); (void)packet(b,1); b[26]^=8; /* Different peer; only its destination header is relevant. */
    CHECK(tq_observe(&p,b,84,1000,0,&s)==1); CHECK(p.cancelled);
    CHECK(tq_observe(&p,b,packet(b,3),1001,0,&s)==0); CHECK(!p.call_seen);
    return 0;
}
int main(void) {
    int failed=serializes_only_vendor_short_query()+owner_changes_never_send()+ack_order_age_and_duplicates()
        +wrong_ack_and_fragment_rejected()+hangup_cancels_query()+default_off_and_bounded_attempts()+conflicts_and_unrelated_calls_latch_cancel();
    printf("time-query probe: 7 checks, %d failures\n",failed); return failed?1:0;
}
