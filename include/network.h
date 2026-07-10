#pragma once

#include "app_types.h"

typedef struct {
    SharedStream *stream;
    Thread thread;
    int socket_fd;
    volatile bool stop_requested;
    bool running;
} NetworkReceiver;

bool network_service_init(char *error, size_t error_size);
void network_service_exit(void);
bool network_receiver_start(NetworkReceiver *receiver, SharedStream *stream, uint16_t port,
                            char *error, size_t error_size);
void network_receiver_stop(NetworkReceiver *receiver);
void network_forget_source(SharedStream *stream);
void network_snapshot(SharedStream *stream, StreamStats *stats, size_t *buffered_bytes,
                      bool *source_active, struct sockaddr_in *source);
