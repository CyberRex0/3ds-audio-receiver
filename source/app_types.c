#include "app_types.h"

size_t app_buffer_target_bytes(uint16_t buffer_ms) {
    size_t bytes = ((size_t)APP_SAMPLE_RATE * APP_FRAME_BYTES * buffer_ms) / 1000;
    return bytes - (bytes % APP_FRAME_BYTES);
}

bool app_config_equal(const AppConfig *left, const AppConfig *right) {
    return left->port == right->port && left->buffer_ms == right->buffer_ms &&
           left->volume == right->volume && left->language == right->language;
}
