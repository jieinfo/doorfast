#include "capture.h"

#include <pcap/pcap.h>
#include <stdlib.h>

struct df_capture {
    pcap_t *handle;
};

const char *df_capture_default_filter(void) {
    return "udp and (port 8300 or port 8302 or port 8303 or port 8304)";
}

int df_capture_open(const char *device, bool promiscuous, struct df_capture **capture) {
    char error_buffer[PCAP_ERRBUF_SIZE] = {0};
    struct df_capture *opened;

    if (device == NULL || device[0] == '\0' || capture == NULL) {
        return DF_ERR_INVALID;
    }
    *capture = NULL;
    opened = calloc(1, sizeof(*opened));
    if (opened == NULL) {
        return DF_ERR_IO;
    }
    opened->handle = pcap_create(device, error_buffer);
    if (opened->handle == NULL || pcap_set_snaplen(opened->handle, 2048) != 0 ||
        pcap_set_timeout(opened->handle, 1000) != 0 ||
        pcap_set_promisc(opened->handle, promiscuous ? 1 : 0) != 0 ||
        pcap_activate(opened->handle) < 0) {
        if (opened->handle != NULL) {
            pcap_close(opened->handle);
        }
        free(opened);
        return DF_ERR_IO;
    }
    if (pcap_datalink(opened->handle) != DLT_EN10MB) {
        pcap_close(opened->handle);
        free(opened);
        return DF_ERR_INVALID;
    }
    *capture = opened;
    return DF_OK;
}

int df_capture_next(struct df_capture *capture, const uint8_t **packet, size_t *length) {
    struct pcap_pkthdr *header = NULL;
    const uint8_t *captured = NULL;
    int result;

    if (capture == NULL || capture->handle == NULL || packet == NULL || length == NULL) {
        return DF_CAPTURE_ERROR;
    }
    *packet = NULL;
    *length = 0;
    result = pcap_next_ex(capture->handle, &header, &captured);
    if (result == 0) {
        return DF_CAPTURE_TIMEOUT;
    }
    if (result != 1 || header == NULL || captured == NULL) {
        return DF_CAPTURE_ERROR;
    }
    *packet = captured;
    *length = header->caplen;
    return DF_CAPTURE_PACKET;
}

int df_capture_set_filter(struct df_capture *capture, const char *bpf) {
    struct bpf_program program;
    int result;

    if (capture == NULL || capture->handle == NULL || bpf == NULL || bpf[0] == '\0') {
        return DF_ERR_INVALID;
    }
    if (pcap_compile(capture->handle, &program, bpf, 1, PCAP_NETMASK_UNKNOWN) != 0) {
        return DF_ERR_INVALID;
    }
    result = pcap_setfilter(capture->handle, &program);
    pcap_freecode(&program);
    return result == 0 ? DF_OK : DF_ERR_IO;
}

void df_capture_close(struct df_capture *capture) {
    if (capture == NULL) {
        return;
    }
    if (capture->handle != NULL) {
        pcap_close(capture->handle);
    }
    free(capture);
}
