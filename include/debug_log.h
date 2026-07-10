#pragma once

#include "app_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_LOG_SAMPLE_CAPACITY 2048

typedef struct {
    uint64_t elapsed_ms;
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t invalid_packets;
    uint64_t foreign_packets;
    uint64_t dropped_bytes;
    uint64_t audio_bytes_dequeued;
    uint64_t audio_buffers_queued;
    uint32_t underruns;
    uint32_t audio_resets;
    uint32_t ndsp_sample_pos;
    uint32_t ndsp_frame_count;
    size_t ring_bytes;
    float ndsp_rate;
    uint16_t ndsp_format;
    uint16_t active_sequence;
    uint8_t wave_status[3];
    uint16_t wave_sequence[3];
    bool source_active;
    bool receiver_running;
    bool playing;
} DebugSample;

typedef struct {
    DebugSample *samples;
    size_t count;
    size_t next;
    uint64_t started_ms;
    uint64_t next_sample_ms;
    AppConfig config;
    bool wrapped;
} DebugLog;

bool debug_log_init(DebugLog *log, const AppConfig *config);
bool debug_log_sample_due(const DebugLog *log, uint64_t now_ms);
void debug_log_record(DebugLog *log, const DebugSample *sample, uint64_t now_ms);
bool debug_log_save(DebugLog *log, char *error, size_t error_size);
void debug_log_destroy(DebugLog *log);
