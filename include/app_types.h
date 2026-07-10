#pragma once

#include <3ds.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

#define APP_SAMPLE_RATE 32728
#define APP_FRAME_BYTES AUDIO_RING_FRAME_BYTES
#define APP_MAX_PACKET_BYTES 1400
#define APP_RING_CAPACITY (64 * 1024)

#define APP_DEFAULT_PORT 5000
#define APP_MIN_PORT 1024
#define APP_MAX_PORT 65535
#define APP_DEFAULT_BUFFER_MS 100
#define APP_MIN_BUFFER_MS 40
#define APP_MAX_BUFFER_MS 250
#define APP_BUFFER_STEP_MS 10
#define APP_DEFAULT_VOLUME 100
#define APP_VOLUME_STEP 5

typedef enum {
    APP_LANGUAGE_JAPANESE = 0,
    APP_LANGUAGE_ENGLISH = 1,
} AppLanguage;

typedef struct {
    uint16_t port;
    uint16_t buffer_ms;
    uint8_t volume;
    AppLanguage language;
} AppConfig;

typedef struct {
    uint64_t bytes_received;
    uint64_t packets_received;
    uint64_t invalid_packets;
    uint64_t foreign_packets;
    uint64_t dropped_bytes;
    uint32_t underruns;
} StreamStats;

typedef struct {
    LightLock lock;
    AudioRingBuffer ring;
    StreamStats stats;
    bool source_active;
    struct sockaddr_in source;
    uint64_t source_last_seen_ms;
} SharedStream;

size_t app_buffer_target_bytes(uint16_t buffer_ms);
bool app_config_equal(const AppConfig *left, const AppConfig *right);
