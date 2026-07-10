#include "ring_buffer.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    AudioRingBuffer ring;
    assert(ring_buffer_init(&ring, 16));

    const uint8_t first[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    size_t dropped = 99;
    assert(ring_buffer_write(&ring, first, sizeof(first), &dropped) == 12);
    assert(dropped == 0);

    uint8_t output[16] = {0};
    assert(ring_buffer_read(&ring, output, 8) == 8);
    assert(memcmp(output, first, 8) == 0);

    const uint8_t second[16] = {12, 13, 14, 15, 16, 17, 18, 19,
                                20, 21, 22, 23, 24, 25, 26, 27};
    assert(ring_buffer_write(&ring, second, sizeof(second), &dropped) == 16);
    assert(dropped == 4);
    assert(ring_buffer_read(&ring, output, sizeof(output)) == 16);
    assert(memcmp(output, second, sizeof(second)) == 0);

    ring_buffer_clear(&ring);
    assert(ring.size == 0);
    ring_buffer_destroy(&ring);
    return 0;
}
