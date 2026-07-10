#pragma once

#include "app_types.h"

#define AUDIO_WAVE_BUFFER_COUNT 3
#define AUDIO_BLOCK_FRAMES 1024
#define AUDIO_NDSP_SAMPLES_PER_BLOCK AUDIO_BLOCK_FRAMES

typedef struct {
    bool initialized;
    bool playing;
    uint64_t bytes_dequeued;
    uint64_t buffers_queued;
    uint32_t resets;
    float current_rate;
    uint32_t ndsp_sample_pos;
    uint32_t ndsp_frame_count;
    uint16_t ndsp_format;
    uint16_t active_sequence;
    uint8_t wave_status[AUDIO_WAVE_BUFFER_COUNT];
    uint16_t wave_sequence[AUDIO_WAVE_BUFFER_COUNT];
} AudioEngineSnapshot;

typedef struct {
    ndspWaveBuf wave_buffers[AUDIO_WAVE_BUFFER_COUNT];
    uint8_t *audio_data;
    size_t block_bytes;
    SharedStream *stream;
    Thread thread;
    LightLock lock;
    LightEvent wake_event;
    LightEvent control_done_event;
    volatile bool stop_requested;
    bool reset_requested;
    bool volume_dirty;
    bool initialized;
    bool playing;
    uint8_t volume;
    uint16_t target_ms;
    uint64_t next_rate_update_ms;
    uint64_t bytes_dequeued;
    uint64_t buffers_queued;
    uint32_t resets;
    float current_rate;
} AudioEngine;

bool audio_engine_init(AudioEngine *engine, SharedStream *stream, uint16_t target_ms,
                       uint8_t volume, char *error, size_t error_size);
void audio_engine_exit(AudioEngine *engine);
void audio_engine_reset(AudioEngine *engine);
void audio_engine_set_volume(AudioEngine *engine, uint8_t volume);
void audio_engine_set_target(AudioEngine *engine, uint16_t target_ms);
void audio_engine_snapshot(AudioEngine *engine, AudioEngineSnapshot *snapshot);
