#include "debug_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DEBUG_LOG_DIR "sdmc:/3ds/3ds-audio-receiver"
#define DEBUG_LOG_PATH DEBUG_LOG_DIR "/debug.log"
#define DEBUG_LOG_INTERVAL_MS 250

bool debug_log_init(DebugLog *log, const AppConfig *config) {
    memset(log, 0, sizeof(*log));
    log->samples = calloc(DEBUG_LOG_SAMPLE_CAPACITY, sizeof(DebugSample));
    if (!log->samples) return false;
    log->config = *config;
    log->started_ms = osGetTime();
    log->next_sample_ms = log->started_ms;
    return true;
}

bool debug_log_sample_due(const DebugLog *log, uint64_t now_ms) {
    return log->samples && now_ms >= log->next_sample_ms;
}

void debug_log_record(DebugLog *log, const DebugSample *sample, uint64_t now_ms) {
    if (!log->samples) return;
    log->samples[log->next] = *sample;
    log->next = (log->next + 1) % DEBUG_LOG_SAMPLE_CAPACITY;
    if (log->count < DEBUG_LOG_SAMPLE_CAPACITY)
        log->count++;
    else
        log->wrapped = true;
    log->next_sample_ms = now_ms + DEBUG_LOG_INTERVAL_MS;
}

static void set_error(char *error, size_t error_size, const char *operation) {
    if (error && error_size)
        snprintf(error, error_size, "%s: %s", operation, strerror(errno));
}

static bool write_sample(FILE *file, const DebugSample *sample) {
    return fprintf(
               file,
               "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%lu,%lu,%zu,%.3f,%u,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
               (unsigned long long)sample->elapsed_ms,
               (unsigned long long)sample->rx_bytes,
               (unsigned long long)sample->rx_packets,
               (unsigned long long)sample->invalid_packets,
               (unsigned long long)sample->foreign_packets,
               (unsigned long long)sample->dropped_bytes,
               (unsigned long long)sample->audio_bytes_dequeued,
               (unsigned long long)sample->audio_buffers_queued,
               (unsigned long)sample->underruns, (unsigned long)sample->audio_resets,
               sample->ring_bytes, sample->ndsp_rate, sample->ndsp_format,
               (unsigned long)sample->ndsp_sample_pos,
               (unsigned long)sample->ndsp_frame_count, sample->active_sequence,
               sample->wave_status[0], sample->wave_status[1], sample->wave_status[2],
               sample->wave_sequence[0], sample->wave_sequence[1],
               sample->wave_sequence[2], sample->source_active, sample->receiver_running,
               sample->playing) >= 0;
}

bool debug_log_save(DebugLog *log, char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!log->samples) {
        if (error && error_size) snprintf(error, error_size, "debug log memory unavailable");
        return false;
    }
    if (mkdir("sdmc:/3ds", 0777) != 0 && errno != EEXIST) {
        set_error(error, error_size, "mkdir sdmc:/3ds");
        return false;
    }
    if (mkdir(DEBUG_LOG_DIR, 0777) != 0 && errno != EEXIST) {
        set_error(error, error_size, "mkdir debug directory");
        return false;
    }

    FILE *file = fopen(DEBUG_LOG_PATH, "w");
    if (!file) {
        set_error(error, error_size, "open debug.log");
        return false;
    }
    fprintf(file, "# 3DS Audio Receiver diagnostic log v1\n");
    fprintf(file, "# sample_rate=%d frame_bytes=%d port=%u target_buffer_ms=%u volume=%u wrapped=%u\n",
            APP_SAMPLE_RATE, APP_FRAME_BYTES, log->config.port, log->config.buffer_ms,
            log->config.volume, log->wrapped);
    fprintf(file,
            "elapsed_ms,rx_bytes,rx_packets,invalid_packets,foreign_packets,dropped_bytes,"
            "audio_bytes_dequeued,audio_buffers_queued,underruns,audio_resets,ring_bytes,"
            "ndsp_rate,ndsp_format,ndsp_sample_pos,ndsp_frame_count,active_sequence,"
            "wave0_status,wave1_status,wave2_status,wave0_sequence,wave1_sequence,"
            "wave2_sequence,source_active,receiver_running,playing\n");

    size_t first = log->wrapped ? log->next : 0;
    bool ok = true;
    for (size_t i = 0; i < log->count; ++i) {
        size_t index = (first + i) % DEBUG_LOG_SAMPLE_CAPACITY;
        if (!write_sample(file, &log->samples[index])) {
            ok = false;
            break;
        }
    }
    if (fclose(file) != 0) ok = false;
    if (!ok) set_error(error, error_size, "write debug.log");
    return ok;
}

void debug_log_destroy(DebugLog *log) {
    free(log->samples);
    memset(log, 0, sizeof(*log));
}
