#include "time_query_probe.h"
#include <string.h>
#include "gvs_packet.h"
#include "gvs_frame.h"
#include "gvs_serialize.h"
#include "gvs_vendor_header.h"

void tq_init(struct tq_probe *p, const struct tq_config *c) { memset(p, 0, sizeof(*p)); p->config = *c; }
void tq_cancel(struct tq_probe *p,enum tq_cancel_reason reason) {
    if (p!=NULL && !p->cancelled && reason!=TQ_CANCEL_NONE) {
        p->cancelled=true; p->cancel_reason=reason;
    }
}
const char *tq_cancel_reason_name(enum tq_cancel_reason reason) {
    switch(reason) {
    case TQ_CANCEL_NONE: return "none";
    case TQ_CANCEL_OWNER_CONFLICT: return "owner_conflict";
    case TQ_CANCEL_CALL_OWNER_CONFLICT: return "call_owner_conflict";
    case TQ_CANCEL_STATUS_READ_FAILED: return "status_read_failed";
    case TQ_CANCEL_INCOMING_CALL: return "incoming_call";
    case TQ_CANCEL_LOCAL_HANGUP: return "local_hangup";
    case TQ_CANCEL_PEER_HANGUP: return "peer_hangup";
    case TQ_CANCEL_CAPTURE_FAILED: return "capture_failed";
    case TQ_CANCEL_INTERRUPTED: return "interrupted";
    case TQ_CANCEL_OBSERVATION_TIMEOUT: return "observation_timeout";
    }
    return "unknown";
}
static bool same_operation(const struct tq_probe *p, const struct tq_owner *s) {
    return s != NULL && strcmp(s->runtime,p->config.runtime)==0 &&
        strcmp(s->station,p->config.station)==0 && s->operation==p->config.operation &&
        s->generation>0 && s->generation==s->native_generation;
}
static bool native_owned(const struct tq_probe *p, const struct tq_owner *s) {
    /* The real status serializer leaves native station empty in PREVIEW. */
    return (strcmp(s->native_state,"preview")==0 &&
            (s->native_station[0]==0 || strcmp(s->native_station,p->config.station)==0)) ||
        (strcmp(s->native_state,"talking")==0 && strcmp(s->native_station,p->config.station)==0);
}

bool tq_check_owner(struct tq_probe *p, const struct tq_owner *s) {
    bool valid=false;
    if (p==NULL || p->cancelled) return false;
    if (s && strcmp(s->runtime,p->config.runtime)==0) {
        if (p->call_seen) {
            valid=same_operation(p,s) && s->generation==p->generation &&
                (strcmp(s->stage,"calling")==0 || strcmp(s->stage,"talking")==0) &&
                native_owned(p,s);
        } else if (s->operation<p->config.operation && s->operation+1==p->config.operation) {
            valid=s->media_empty && (strcmp(s->stage,"idle")==0 || strcmp(s->stage,"ended")==0 || strcmp(s->stage,"failed")==0) &&
                (strcmp(s->native_state,"idle")==0 || strcmp(s->native_state,"ended")==0);
        } else if (s->operation==p->config.operation && strcmp(s->station,p->config.station)==0) {
            valid=(strcmp(s->stage,"preparing")==0 || strcmp(s->stage,"calling")==0 || strcmp(s->stage,"talking")==0) &&
                (strcmp(s->native_state,"idle")==0 || strcmp(s->native_state,"ended")==0 ||
                 (same_operation(p,s) && native_owned(p,s)));
        }
    }
    if (!valid) tq_cancel(p,TQ_CANCEL_OWNER_CONFLICT);
    return valid;
}

int tq_observe(struct tq_probe *p, const uint8_t *b, size_t n, uint64_t now,
               unsigned age, const struct tq_owner *s) {
    struct df_udp_prefix u;
    struct df_gvs_frame f;
    struct df_event e;
    static const uint8_t magic[10]={'G','V','S','G','V','S',0xa5,0xa5,0xa5,0xa5};
    bool outgoing;
    if (p==NULL || b==NULL || p->cancelled || age>100 || age>now ||
        n<42 || b[12]!=8 || b[13]!=0 || b[14]!=0x45 ||
        (b[20]&0x3f)!=0 || b[21]!=0 ||
        df_gvs_inspect_udp_prefix(b,n,&u)!=1 || u.source_port!=8300 || u.destination_port!=8300)
        return 0;
    if (!tq_check_owner(p,s)) return 0;
    /* Only an incoming control header addressed to this indoor identity is
     * needed to abort. Never parse or retain the caller's identity payload. */
    if (u.destination_ipv4==p->config.local_ip && u.captured_payload_length>=42 &&
        memcmp(b+u.payload_offset,magic,10)==0 && memcmp(b+u.payload_offset+10,p->config.local,6)==0 &&
        b[u.payload_offset+38]==3 && b[u.payload_offset+39]==1) {
        tq_cancel(p,TQ_CANCEL_INCOMING_CALL); return 1;
    }
    if (!u.payload_complete ||
        df_gvs_frame_parse(b+u.payload_offset,u.declared_payload_length,&f,&e)!=0 || f.family!=3)
        return 0;
    outgoing=u.source_ipv4==p->config.local_ip && u.destination_ipv4==p->config.peer_ip;
    if (!outgoing && !(u.source_ipv4==p->config.peer_ip && u.destination_ipv4==p->config.local_ip)) return 0;
    if (memcmp(f.source,outgoing?p->config.local:p->config.peer,6)!=0 ||
        memcmp(f.destination,outgoing?p->config.peer:p->config.local,6)!=0) return 0;
    if (f.opcode==2) {
        /* Preview retries stop before the formal call exists. Never carry
         * this exception across a CALL, an owner change or an active native
         * session; peer stops and every other stop payload still cancel. */
        if (outgoing && !p->call_seen && !p->ack_seen &&
            s->operation==p->config.operation && strcmp(s->station,p->config.station)==0 &&
            strcmp(s->stage,"preparing")==0 && s->generation==0 &&
            (strcmp(s->native_state,"idle")==0 || strcmp(s->native_state,"ended")==0) &&
            s->native_station[0]==0 && f.payload_length==1 && f.payload[0]==0) {
            p->preparation_stops++; return 1;
        }
        tq_cancel(p,outgoing?TQ_CANCEL_LOCAL_HANGUP:TQ_CANCEL_PEER_HANGUP); return 1;
    }
    if (f.payload_length!=7 || f.payload[1]!=0x20 || f.payload[2]!=0x6f ||
        f.payload[3]!=0 || f.payload[4]!=0x20 || f.payload[5]!=0x6e || f.payload[6]==0) return 0;
    if (outgoing && f.opcode==3 && f.payload[0]==2 && f.payload[6]==120 && !p->call_seen) {
        if (!same_operation(p,s) || (strcmp(s->stage,"calling")!=0 && strcmp(s->stage,"talking")!=0)) {
            tq_cancel(p,TQ_CANCEL_CALL_OWNER_CONFLICT); return 0;
        }
        p->call_seen=true; p->call_ms=now-age; p->generation=s->generation;
        memcpy(p->ethernet_ip_udp,b,42); return 1;
    }
    if (!outgoing && f.opcode==0x83 && f.payload[0]==0 && p->call_seen && !p->ack_seen &&
        now-age>=p->call_ms && now-age-p->call_ms<=1000 && same_operation(p,s) && s->generation==p->generation) {
        p->ack_seen=true; p->ack_ms=now-age; return 1;
    }
    return 0;
}
bool tq_may_send(const struct tq_probe *p, const struct tq_owner *s, uint64_t now) {
    return p!=NULL && p->config.send && !p->cancelled && p->call_seen && p->ack_seen &&
        p->attempts<2 && now>=p->ack_ms && now-p->ack_ms<=100 && same_operation(p,s) &&
        s->generation==p->generation && strcmp(s->stage,"talking")==0 &&
        strcmp(s->native_state,"talking")==0 && strcmp(s->native_station,p->config.station)==0;
}
static void be16(uint8_t *p,unsigned v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
int tq_send(struct tq_probe *p, const struct tq_owner *s, uint64_t now,
            tq_emit_fn emit, void *context) {
    uint8_t b[84]; size_t length=0; unsigned checksum=0;
    if (p==NULL || !tq_check_owner(p,s) || !tq_may_send(p,s,now) || emit==NULL) return -1;
    p->attempts++; /* A failed send consumes its slot, too. */
    memcpy(b,p->ethernet_ip_udp,42);
    if (df_gvs_control_serialize(b+42,42,&length,p->config.peer,p->config.local,3,0x56,NULL,0,
                                df_gvs_vendor_header_fields,NULL)!=0 || length!=42) return -1;
    /* Evidence-backed vendor quirk: length says 1, but no payload is sent.
     * Keep this isolated from the production serializer/parser contract. */
    b[82]=1; b[83]=0;
    be16(b+16,70); be16(b+18,((unsigned)b[64]<<8)|b[65]); be16(b+24,0);
    be16(b+38,50); be16(b+40,0); /* IPv4 UDP checksum omission is legal. */
    for (unsigned i=14;i<34;i+=2) checksum+=((unsigned)b[i]<<8)|b[i+1];
    while (checksum>>16) checksum=(checksum&0xffff)+(checksum>>16);
    be16(b+24,(~checksum)&0xffff);
    if (emit(b,sizeof(b),context)!=0) return -1;
    p->sent++; return 0;
}
