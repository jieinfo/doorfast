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

int main(void) {
    uint8_t bytes[84]={0};
    struct tq_probe p={0};
    struct transport t={NULL,&p};
    p.ack_ms=milliseconds(CLOCK_MONOTONIC);
    if(inject(bytes,sizeof(bytes),&t)!=0 || writes!=1) return 1;
    p.ack_ms=milliseconds(CLOCK_MONOTONIC)-101;
    if(inject(bytes,sizeof(bytes),&t)!=-1 || writes!=1) return 1;
    p.ack_ms=milliseconds(CLOCK_MONOTONIC); interrupted=1;
    if(inject(bytes,sizeof(bytes),&t)!=-1 || writes!=1) return 1;
    puts("final query IO guard: fresh write admitted; expired/interrupted write rejected");
    return 0;
}
