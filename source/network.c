#include "network.h"

#include "ring_buffer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFER_SIZE 0x100000
#define RECEIVER_STACK_SIZE (32 * 1024)
#define SOURCE_TIMEOUT_MS 2000

static u32 *soc_buffer;
static bool soc_ready;

static void set_error(char *error, size_t error_size, const char *operation, int code) {
    if (error && error_size) {
        snprintf(error, error_size, "%s failed: %d (%s)", operation, code, strerror(code));
    }
}

bool network_service_init(char *error, size_t error_size) {
    if (soc_ready) return true;
    soc_buffer = memalign(SOC_ALIGN, SOC_BUFFER_SIZE);
    if (!soc_buffer) {
        if (error && error_size) snprintf(error, error_size, "SOC memory allocation failed");
        return false;
    }
    Result result = socInit(soc_buffer, SOC_BUFFER_SIZE);
    if (R_FAILED(result)) {
        if (error && error_size)
            snprintf(error, error_size, "socInit failed: 0x%08lX", (unsigned long)result);
        free(soc_buffer);
        soc_buffer = NULL;
        return false;
    }
    soc_ready = true;
    return true;
}

void network_service_exit(void) {
    if (soc_ready) socExit();
    free(soc_buffer);
    soc_buffer = NULL;
    soc_ready = false;
}

static bool same_source_ip(const struct sockaddr_in *left, const struct sockaddr_in *right) {
    return left->sin_addr.s_addr == right->sin_addr.s_addr;
}

static void expire_inactive_source(SharedStream *stream, uint64_t now) {
    LightLock_Lock(&stream->lock);
    if (stream->source_active && now - stream->source_last_seen_ms > SOURCE_TIMEOUT_MS) {
        stream->source_active = false;
        memset(&stream->source, 0, sizeof(stream->source));
        stream->source_last_seen_ms = 0;
        ring_buffer_clear(&stream->ring);
    }
    LightLock_Unlock(&stream->lock);
}

static void receiver_thread(void *argument) {
    NetworkReceiver *receiver = argument;
    uint8_t packet[APP_MAX_PACKET_BYTES + 1];

    while (!receiver->stop_requested) {
        expire_inactive_source(receiver->stream, osGetTime());
        struct sockaddr_in sender;
        socklen_t sender_size = sizeof(sender);
        ssize_t received = recvfrom(receiver->socket_fd, packet, sizeof(packet), 0,
                                    (struct sockaddr *)&sender, &sender_size);
        uint64_t now = osGetTime();

        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
                !receiver->stop_requested) {
                svcSleepThread(5 * 1000 * 1000LL);
            } else {
                svcSleepThread(1 * 1000 * 1000LL);
            }
            continue;
        }

        SharedStream *stream = receiver->stream;
        LightLock_Lock(&stream->lock);
        if (stream->source_active && !same_source_ip(&stream->source, &sender)) {
            stream->stats.foreign_packets++;
            LightLock_Unlock(&stream->lock);
            continue;
        }

        if (received == 0 || received > APP_MAX_PACKET_BYTES ||
            ((size_t)received % APP_FRAME_BYTES) != 0) {
            stream->stats.invalid_packets++;
            LightLock_Unlock(&stream->lock);
            continue;
        }

        if (!stream->source_active) {
            stream->source = sender;
            stream->source_active = true;
        }
        stream->source_last_seen_ms = now;
        size_t dropped = 0;
        ring_buffer_write(&stream->ring, packet, (size_t)received, &dropped);
        stream->stats.bytes_received += (uint64_t)received;
        stream->stats.packets_received++;
        stream->stats.dropped_bytes += dropped;
        LightLock_Unlock(&stream->lock);
    }
}

bool network_receiver_start(NetworkReceiver *receiver, SharedStream *stream, uint16_t port,
                            char *error, size_t error_size) {
    memset(receiver, 0, sizeof(*receiver));
    receiver->socket_fd = -1;
    receiver->stream = stream;
    receiver->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiver->socket_fd < 0) {
        set_error(error, error_size, "socket", errno);
        return false;
    }

    int flags = fcntl(receiver->socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(receiver->socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        set_error(error, error_size, "fcntl", errno);
        close(receiver->socket_fd);
        receiver->socket_fd = -1;
        return false;
    }

    int receive_buffer = 128 * 1024;
    setsockopt(receiver->socket_fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
               sizeof(receive_buffer));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(receiver->socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        set_error(error, error_size, "bind", errno);
        close(receiver->socket_fd);
        receiver->socket_fd = -1;
        return false;
    }

    receiver->stop_requested = false;
    receiver->thread = threadCreate(receiver_thread, receiver, RECEIVER_STACK_SIZE, 0x31, -2,
                                    false);
    if (!receiver->thread) {
        if (error && error_size) snprintf(error, error_size, "receiver thread creation failed");
        close(receiver->socket_fd);
        receiver->socket_fd = -1;
        return false;
    }
    receiver->running = true;
    return true;
}

void network_receiver_stop(NetworkReceiver *receiver) {
    if (!receiver->running) return;
    receiver->stop_requested = true;
    if (receiver->socket_fd >= 0) {
        close(receiver->socket_fd);
        receiver->socket_fd = -1;
    }
    threadJoin(receiver->thread, U64_MAX);
    threadFree(receiver->thread);
    receiver->thread = NULL;
    receiver->running = false;
}

void network_forget_source(SharedStream *stream) {
    LightLock_Lock(&stream->lock);
    stream->source_active = false;
    memset(&stream->source, 0, sizeof(stream->source));
    stream->source_last_seen_ms = 0;
    ring_buffer_clear(&stream->ring);
    LightLock_Unlock(&stream->lock);
}

void network_snapshot(SharedStream *stream, StreamStats *stats, size_t *buffered_bytes,
                      bool *source_active, struct sockaddr_in *source) {
    LightLock_Lock(&stream->lock);
    *stats = stream->stats;
    *buffered_bytes = stream->ring.size;
    *source_active = stream->source_active;
    *source = stream->source;
    LightLock_Unlock(&stream->lock);
}
