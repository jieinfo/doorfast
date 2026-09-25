#include "gvs_media_receiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int df_gvs_media_receiver_open_socket(const char *host, uint16_t port,
    int *fd, uint16_t *bound_port) {
    struct sockaddr_in local = {0};
    struct in_addr requested_address;
    socklen_t local_length = sizeof(local);
    int receive_buffer = (int)DF_GVS_MEDIA_RECEIVE_BUFFER_BYTES;
    int flags;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd < 0) return DF_ERR_IO;
    flags = fcntl(*fd, F_GETFL, 0);
    if (flags < 0 || fcntl(*fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(*fd);
        *fd = -1;
        return DF_ERR_IO;
    }
#ifdef SO_RCVBUFFORCE
    if (setsockopt(*fd, SOL_SOCKET, SO_RCVBUFFORCE, &receive_buffer,
        sizeof(receive_buffer)) != 0 &&
        setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
            sizeof(receive_buffer)) != 0) {
#else
    if (setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
        sizeof(receive_buffer)) != 0) {
#endif
        (void)close(*fd);
        *fd = -1;
        return DF_ERR_IO;
    }
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    /*
     * The configured address may be a non-canonical host address such as
     * 10.5.83.0/8.  The station packets are addressed to that value, but
     * binding a socket to it can leave the kernel socket receive path empty.
     * Validate the configured IPv4 text, then bind wildcard and enforce the
     * GVS destination/source identity in the media pipeline instead.
     */
    if (inet_pton(AF_INET, host, &requested_address) != 1) {
        (void)close(*fd);
        *fd = -1;
        return DF_ERR_IO;
    }
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(*fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        getsockname(*fd, (struct sockaddr *)&local, &local_length) != 0 ||
        local_length != sizeof(local)) {
        (void)close(*fd);
        *fd = -1;
        return DF_ERR_IO;
    }
    *bound_port = ntohs(local.sin_port);
    if (*bound_port == 0U) {
        (void)close(*fd);
        *fd = -1;
        return DF_ERR_IO;
    }
    return DF_OK;
}

int df_gvs_media_receiver_open(struct df_gvs_media_receiver *receiver,
    const char *host, uint16_t audio_port, uint16_t video_port) {
    if (receiver == NULL || host == NULL || host[0] == '\0' ||
        (audio_port != 0U && audio_port == video_port)) return DF_ERR_INVALID;
    memset(receiver, 0, sizeof(*receiver));
    receiver->audio_fd = -1;
    receiver->video_fd = -1;
    receiver->receive_buffer_bytes = DF_GVS_MEDIA_RECEIVE_BUFFER_BYTES;
    if (df_gvs_media_receiver_open_socket(host, audio_port,
            &receiver->audio_fd, &receiver->audio_port) != DF_OK ||
        df_gvs_media_receiver_open_socket(host, video_port,
            &receiver->video_fd, &receiver->video_port) != DF_OK ||
        receiver->audio_port == receiver->video_port) {
        df_gvs_media_receiver_close(receiver);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static int df_gvs_media_receiver_read(struct df_gvs_media_receiver *receiver,
    int fd, enum df_gvs_media_channel channel, uint8_t *buffer,
    size_t capacity, struct df_gvs_media_datagram *datagram) {
    struct sockaddr_in source = {0};
    struct iovec vector = {
        .iov_base = buffer,
        .iov_len = capacity,
    };
    struct msghdr message = {
        .msg_name = &source,
        .msg_namelen = sizeof(source),
        .msg_iov = &vector,
        .msg_iovlen = 1U,
    };
    ssize_t received = recvmsg(fd, &message, MSG_DONTWAIT);

    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return DF_GVS_MEDIA_RECEIVER_EMPTY;
    if (received < 0 || message.msg_namelen != sizeof(source) ||
        source.sin_family != AF_INET) {
        receiver->receive_errors++;
        return DF_GVS_MEDIA_RECEIVER_ERROR;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        receiver->truncated++;
        return DF_GVS_MEDIA_RECEIVER_DROPPED;
    }
    datagram->channel = channel;
    datagram->source_ipv4 = source.sin_addr.s_addr;
    datagram->length = (size_t)received;
    if (channel == DF_GVS_MEDIA_AUDIO) receiver->audio_received++;
    else receiver->video_received++;
    return DF_GVS_MEDIA_RECEIVER_DATAGRAM;
}

int df_gvs_media_receiver_next(struct df_gvs_media_receiver *receiver,
    uint8_t *buffer, size_t capacity,
    struct df_gvs_media_datagram *datagram) {
    unsigned attempt;

    if (datagram != NULL) memset(datagram, 0, sizeof(*datagram));
    if (receiver == NULL || receiver->audio_fd < 0 || receiver->video_fd < 0 ||
        buffer == NULL || capacity == 0U || datagram == NULL)
        return DF_GVS_MEDIA_RECEIVER_ERROR;
    for (attempt = 0U; attempt < 2U; attempt++) {
        enum df_gvs_media_channel channel = receiver->next_channel == 0U ?
            DF_GVS_MEDIA_VIDEO : DF_GVS_MEDIA_AUDIO;
        int fd = channel == DF_GVS_MEDIA_VIDEO ? receiver->video_fd :
            receiver->audio_fd;
        int result;

        receiver->next_channel ^= 1U;
        result = df_gvs_media_receiver_read(receiver, fd, channel, buffer,
            capacity, datagram);
        if (result != DF_GVS_MEDIA_RECEIVER_EMPTY) return result;
    }
    return DF_GVS_MEDIA_RECEIVER_EMPTY;
}

void df_gvs_media_receiver_close(struct df_gvs_media_receiver *receiver) {
    if (receiver == NULL) return;
    if (receiver->audio_fd >= 0) (void)close(receiver->audio_fd);
    if (receiver->video_fd >= 0) (void)close(receiver->video_fd);
    receiver->audio_fd = -1;
    receiver->video_fd = -1;
}
