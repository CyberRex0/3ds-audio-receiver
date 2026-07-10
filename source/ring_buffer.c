#include "ring_buffer.h"

#include <stdlib.h>
#include <string.h>

bool ring_buffer_init(AudioRingBuffer *ring, size_t capacity) {
    memset(ring, 0, sizeof(*ring));
    capacity -= capacity % AUDIO_RING_FRAME_BYTES;
    if (capacity == 0) return false;
    ring->data = malloc(capacity);
    if (!ring->data) return false;
    ring->capacity = capacity;
    return true;
}

void ring_buffer_destroy(AudioRingBuffer *ring) {
    free(ring->data);
    memset(ring, 0, sizeof(*ring));
}

void ring_buffer_clear(AudioRingBuffer *ring) {
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->size = 0;
}

static void copy_into_ring(AudioRingBuffer *ring, const uint8_t *data, size_t length) {
    size_t first = ring->capacity - ring->write_pos;
    if (first > length) first = length;
    memcpy(ring->data + ring->write_pos, data, first);
    memcpy(ring->data, data + first, length - first);
    ring->write_pos = (ring->write_pos + length) % ring->capacity;
    ring->size += length;
}

size_t ring_buffer_write(AudioRingBuffer *ring, const uint8_t *data, size_t length,
                         size_t *dropped_bytes) {
    if (dropped_bytes) *dropped_bytes = 0;
    length -= length % AUDIO_RING_FRAME_BYTES;
    if (!length || !ring->data) return 0;

    if (length > ring->capacity) {
        size_t skip = length - ring->capacity;
        skip += (AUDIO_RING_FRAME_BYTES - (skip % AUDIO_RING_FRAME_BYTES)) %
                AUDIO_RING_FRAME_BYTES;
        data += skip;
        length -= skip;
        if (dropped_bytes) *dropped_bytes += skip;
    }

    size_t needed = length > ring->capacity - ring->size
                        ? length - (ring->capacity - ring->size)
                        : 0;
    if (needed) {
        needed += (AUDIO_RING_FRAME_BYTES - (needed % AUDIO_RING_FRAME_BYTES)) %
                  AUDIO_RING_FRAME_BYTES;
        ring->read_pos = (ring->read_pos + needed) % ring->capacity;
        ring->size -= needed;
        if (dropped_bytes) *dropped_bytes += needed;
    }

    copy_into_ring(ring, data, length);
    return length;
}

size_t ring_buffer_read(AudioRingBuffer *ring, uint8_t *destination, size_t length) {
    length -= length % AUDIO_RING_FRAME_BYTES;
    if (length > ring->size) length = ring->size;
    if (!length) return 0;

    size_t first = ring->capacity - ring->read_pos;
    if (first > length) first = length;
    memcpy(destination, ring->data + ring->read_pos, first);
    memcpy(destination + first, ring->data, length - first);
    ring->read_pos = (ring->read_pos + length) % ring->capacity;
    ring->size -= length;
    return length;
}
