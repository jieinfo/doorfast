/* Exercise the actual final adapter guard, replacing only physical packet IO. */
#include <pcap/pcap.h>
static unsigned writes;
static int test_packet_write(pcap_t *p,const void *b,size_t n) {
    (void)p; (void)b; writes++; return (int)n;
}
#define pcap_inject test_packet_write
#define main tq_program_main
#include "../tools/diagnostics/time_query_probe_main.c"
#undef main
#undef pcap_inject

int main(int argc,char **argv) {
    uint8_t bytes[84]={0};
    struct tq_probe p={0};
    struct transport t={NULL,&p};
    if(argc==2 && (strcmp(argv[1],"--cancel-report")==0 || strcmp(argv[1],"--read-failure-report")==0)) {
        struct tq_config c={.runtime="1111111111111111"};
        struct tq_owner s={.runtime="2222222222222222"};
        tq_init(&p,&c);
        if(strcmp(argv[1],"--read-failure-report")==0) {
            struct reader r={0};
            if(refresh_owner(&p,&r,&s)) return 1;
        } else if(tq_check_owner(&p,&s)) return 1;
        /* Later causes cannot overwrite the first observed failure. */
        tq_cancel(&p,TQ_CANCEL_INTERRUPTED);
        report_finished(&p,true,1,1234);
        return 0;
    }
    p.ack_ms=milliseconds(CLOCK_MONOTONIC);
    if(inject(bytes,sizeof(bytes),&t)!=0 || writes!=1) return 1;
    p.ack_ms=milliseconds(CLOCK_MONOTONIC)-101;
    if(inject(bytes,sizeof(bytes),&t)!=-1 || writes!=1) return 1;
    p.ack_ms=milliseconds(CLOCK_MONOTONIC); interrupted=1;
    if(inject(bytes,sizeof(bytes),&t)!=-1 || writes!=1 || p.cancel_reason!=TQ_CANCEL_INTERRUPTED) return 1;
    puts("final query IO guard: fresh write admitted; expired/interrupted write rejected");
    return 0;
}
