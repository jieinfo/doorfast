/* Characterize the live pcap boundary without requiring capture privileges.
 * Packet availability follows pcap's immediate versus timeout-batched contract;
 * all adapter lifecycle and record handling below are production code. */
#include <pcap/pcap.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "test.h"

int df_test_failure_count;
static bool delivery_immediate, delivery_activated, delivery_fail_immediate;
static int delivery_timeout;
static uint64_t delivery_now_ms;
static struct pcap_pkthdr delivery_header = {
    .ts = {.tv_sec = 123, .tv_usec = 307000}, .caplen = 4, .len = 4};
static const u_char delivery_bytes[] = {0x03, 0x83, 0, 0};

static pcap_t *delivery_create(const char *device, char *error) {
    (void)device; (void)error;
    delivery_immediate = delivery_activated = false;
    return pcap_open_dead(DLT_EN10MB, 2048);
}
static int delivery_set_integer(pcap_t *handle, int value) {
    (void)handle; (void)value; return 0;
}
static int delivery_set_timeout(pcap_t *handle, int value) {
    (void)handle; delivery_timeout = value; return 0;
}
int delivery_set_immediate(pcap_t *handle, int value) {
    (void)handle;
    if (delivery_fail_immediate || delivery_activated) return PCAP_ERROR;
    delivery_immediate = value != 0;
    return 0;
}
static int delivery_activate(pcap_t *handle) {
    (void)handle; delivery_activated = true; return 0;
}
static int delivery_set_nonblock(pcap_t *handle, int value, char *error) {
    (void)handle; (void)value; (void)error; return 0;
}
static int delivery_next(pcap_t *handle, struct pcap_pkthdr **header,
    const u_char **bytes) {
    (void)handle;
    uint64_t ready = delivery_immediate ? 307U : (uint64_t)delivery_timeout;
    if (delivery_now_ms < ready) return 0;
    *header = &delivery_header;
    *bytes = delivery_bytes;
    return 1;
}

#define pcap_create delivery_create
#define pcap_set_snaplen delivery_set_integer
#define pcap_set_promisc delivery_set_integer
#define pcap_set_timeout delivery_set_timeout
#define pcap_set_immediate_mode delivery_set_immediate
#define pcap_activate delivery_activate
#define pcap_setnonblock delivery_set_nonblock
#define pcap_next_ex delivery_next
#include "../src/capture.c"

int main(void) {
    struct df_capture *capture = NULL;
    struct df_capture_record record;
    TEST_ASSERT_INT_EQ(DF_OK, df_capture_open("fixture", false, &capture));
    delivery_now_ms = 200;
    TEST_ASSERT_INT_EQ(DF_CAPTURE_TIMEOUT, df_capture_next_record(capture, &record));
    /* Actual field ACK arrived at +307ms; it must be available to the
     * consumer at +400ms, before its unchanged +1000ms confirmation timeout. */
    delivery_now_ms = 400;
    int result = df_capture_next_record(capture, &record);
    TEST_ASSERT_INT_EQ(DF_CAPTURE_PACKET, result);
    if (result == DF_CAPTURE_PACKET) {
        TEST_ASSERT_INT_EQ(307000, record.wall_microseconds);
        TEST_ASSERT_INT_EQ(0, memcmp(delivery_bytes, record.data, sizeof(delivery_bytes)));
    }
    df_capture_close(capture);
    capture = NULL;
    delivery_fail_immediate = true;
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_capture_open("fixture", false, &capture));
    TEST_ASSERT_INT_EQ(1, capture == NULL);
    TEST_ASSERT_INT_EQ(0, delivery_activated);
    df_capture_close(capture);
    if (test_failures()) return 1;
    puts("capture delivers control ACKs before confirmation timeout; setup failure fails closed");
    return 0;
}
