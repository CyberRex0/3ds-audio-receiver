#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RING_FRAME_BYTES 4

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t size;
} AudioRingBuffer;

bool ring_buffer_init(AudioRingBuffer *ring, size_t capacity);
void ring_buffer_destroy(AudioRingBuffer *ring);
void ring_buffer_clear(AudioRingBuffer *ring);
size_t ring_buffer_write(AudioRingBuffer *ring, const uint8_t *data, size_t length,
                         size_t *dropped_bytes);
size_t ring_buffer_read(AudioRingBuffer *ring, uint8_t *destination, size_t length);
