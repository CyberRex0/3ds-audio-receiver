#include "audio.h"

#include "ring_buffer.h"

#include <stdio.h>
#include <string.h>

#define AUDIO_THREAD_STACK_SIZE (32 * 1024)
#define AUDIO_RATE_ADJUST_LIMIT 0.015f
#define AUDIO_RATE_CONTROL_GAIN 0.02f
#define AUDIO_RATE_SMOOTHING 0.05f
#define AUDIO_RATE_UPDATE_MS 100

static void set_channel_volume(uint8_t volume) {
    float mix[12] = {0};
    mix[0] = volume / 100.0f;
    mix[1] = volume / 100.0f;
    ndspChnSetMix(0, mix);
}

static void configure_channel(uint8_t volume) {
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, APP_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    set_channel_volume(volume);
}

static void setup_wave_buffers(AudioEngine *engine) {
    for (size_t i = 0; i < AUDIO_WAVE_BUFFER_COUNT; ++i) {
        memset(&engine->wave_buffers[i], 0, sizeof(ndspWaveBuf));
        engine->wave_buffers[i].data_vaddr = engine->audio_data + i * engine->block_bytes;
        engine->wave_buffers[i].nsamples = AUDIO_NDSP_SAMPLES_PER_BLOCK;
    }
}

static size_t prebuffer_bytes(const AudioEngine *engine) {
    size_t configured = app_buffer_target_bytes(engine->target_ms);
    size_t queued = engine->block_bytes * AUDIO_WAVE_BUFFER_COUNT;
    return configured > queued ? configured : queued;
}

static bool fill_wave_buffer_locked(AudioEngine *engine, size_t index) {
    ndspWaveBuf *wave = &engine->wave_buffers[index];
    LightLock_Lock(&engine->stream->lock);
    size_t read = ring_buffer_read(&engine->stream->ring, (uint8_t *)wave->data_vaddr,
                                   engine->block_bytes);
    LightLock_Unlock(&engine->stream->lock);
    if (read != engine->block_bytes) return false;

    DSP_FlushDataCache(wave->data_vaddr, (u32)engine->block_bytes);
    ndspChnWaveBufAdd(0, wave);
    engine->bytes_dequeued += engine->block_bytes;
    engine->buffers_queued++;
    return true;
}

static void reset_playback_locked(AudioEngine *engine, bool count_underrun) {
    ndspChnWaveBufClear(0);
    engine->playing = false;
    engine->resets++;
    engine->current_rate = APP_SAMPLE_RATE;
    engine->next_rate_update_ms = osGetTime() + AUDIO_RATE_UPDATE_MS;
    ndspChnSetRate(0, engine->current_rate);
    setup_wave_buffers(engine);

    LightLock_Lock(&engine->stream->lock);
    if (count_underrun) engine->stream->stats.underruns++;
    ring_buffer_clear(&engine->stream->ring);
    LightLock_Unlock(&engine->stream->lock);
}

static void update_playback_rate_locked(AudioEngine *engine, uint64_t now) {
    if (now < engine->next_rate_update_ms) return;
    engine->next_rate_update_ms = now + AUDIO_RATE_UPDATE_MS;

    size_t available;
    LightLock_Lock(&engine->stream->lock);
    available = engine->stream->ring.size;
    LightLock_Unlock(&engine->stream->lock);

    size_t queued = engine->block_bytes * AUDIO_WAVE_BUFFER_COUNT;
    size_t prebuffer = prebuffer_bytes(engine);
    size_t desired = prebuffer > queued ? prebuffer - queued : engine->block_bytes;
    if (desired < engine->block_bytes) desired = engine->block_bytes;

    float error = ((float)available - (float)desired) / (float)desired;
    float adjustment = error * AUDIO_RATE_CONTROL_GAIN;
    if (adjustment > AUDIO_RATE_ADJUST_LIMIT) adjustment = AUDIO_RATE_ADJUST_LIMIT;
    if (adjustment < -AUDIO_RATE_ADJUST_LIMIT) adjustment = -AUDIO_RATE_ADJUST_LIMIT;
    float target_rate = APP_SAMPLE_RATE * (1.0f + adjustment);
    engine->current_rate += (target_rate - engine->current_rate) * AUDIO_RATE_SMOOTHING;
    ndspChnSetRate(0, engine->current_rate);
}

static void service_audio_locked(AudioEngine *engine) {
    if (engine->reset_requested) {
        reset_playback_locked(engine, false);
        engine->reset_requested = false;
        LightEvent_Signal(&engine->control_done_event);
        return;
    }

    if (engine->volume_dirty) {
        set_channel_volume(engine->volume);
        engine->volume_dirty = false;
    }

    if (!engine->playing) {
        size_t available;
        bool has_source;
        LightLock_Lock(&engine->stream->lock);
        available = engine->stream->ring.size;
        has_source = engine->stream->source_active;
        LightLock_Unlock(&engine->stream->lock);
        if (!has_source || available < prebuffer_bytes(engine)) return;

        for (size_t i = 0; i < AUDIO_WAVE_BUFFER_COUNT; ++i) {
            if (!fill_wave_buffer_locked(engine, i)) {
                reset_playback_locked(engine, true);
                return;
            }
        }
        engine->playing = true;
        engine->next_rate_update_ms = osGetTime() + AUDIO_RATE_UPDATE_MS;
        return;
    }

    for (size_t i = 0; i < AUDIO_WAVE_BUFFER_COUNT; ++i) {
        if (engine->wave_buffers[i].status == NDSP_WBUF_DONE &&
            !fill_wave_buffer_locked(engine, i)) {
            reset_playback_locked(engine, true);
            return;
        }
    }
    update_playback_rate_locked(engine, osGetTime());
}

static void audio_frame_callback(void *argument) {
    AudioEngine *engine = argument;
    if (!engine->stop_requested) LightEvent_Signal(&engine->wake_event);
}

static void audio_thread(void *argument) {
    AudioEngine *engine = argument;
    while (true) {
        LightEvent_Wait(&engine->wake_event);
        LightLock_Lock(&engine->lock);
        if (engine->stop_requested) {
            LightLock_Unlock(&engine->lock);
            break;
        }
        service_audio_locked(engine);
        LightLock_Unlock(&engine->lock);
    }
}

bool audio_engine_init(AudioEngine *engine, SharedStream *stream, uint16_t target_ms,
                       uint8_t volume, char *error, size_t error_size) {
    memset(engine, 0, sizeof(*engine));
    Result result = ndspInit();
    if (R_FAILED(result)) {
        if (error && error_size)
            snprintf(error, error_size, "ndspInit failed: 0x%08lX", (unsigned long)result);
        return false;
    }

    engine->block_bytes = AUDIO_BLOCK_FRAMES * APP_FRAME_BYTES;
    engine->audio_data = linearAlloc(engine->block_bytes * AUDIO_WAVE_BUFFER_COUNT);
    if (!engine->audio_data) {
        if (error && error_size) snprintf(error, error_size, "linear audio allocation failed");
        ndspExit();
        return false;
    }
    memset(engine->audio_data, 0, engine->block_bytes * AUDIO_WAVE_BUFFER_COUNT);
    engine->stream = stream;
    engine->target_ms = target_ms;
    engine->volume = volume;
    engine->current_rate = APP_SAMPLE_RATE;
    LightLock_Init(&engine->lock);
    LightEvent_Init(&engine->wake_event, RESET_ONESHOT);
    LightEvent_Init(&engine->control_done_event, RESET_ONESHOT);
    configure_channel(volume);
    setup_wave_buffers(engine);

    int32_t priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    priority--;
    if (priority < 0x18) priority = 0x18;
    if (priority > 0x3F) priority = 0x3F;

    ndspSetCallback(audio_frame_callback, engine);
    engine->thread = threadCreate(audio_thread, engine, AUDIO_THREAD_STACK_SIZE, priority, -2,
                                  false);
    if (!engine->thread) {
        ndspSetCallback(NULL, NULL);
        if (error && error_size) snprintf(error, error_size, "audio thread creation failed");
        ndspChnWaveBufClear(0);
        linearFree(engine->audio_data);
        ndspExit();
        memset(engine, 0, sizeof(*engine));
        return false;
    }
    engine->initialized = true;
    LightEvent_Signal(&engine->wake_event);
    return true;
}

void audio_engine_exit(AudioEngine *engine) {
    if (!engine->initialized) return;
    LightLock_Lock(&engine->lock);
    engine->stop_requested = true;
    LightLock_Unlock(&engine->lock);
    ndspSetCallback(NULL, NULL);
    LightEvent_Signal(&engine->wake_event);
    threadJoin(engine->thread, U64_MAX);
    threadFree(engine->thread);
    ndspChnWaveBufClear(0);
    ndspExit();
    linearFree(engine->audio_data);
    memset(engine, 0, sizeof(*engine));
}

void audio_engine_reset(AudioEngine *engine) {
    if (!engine->initialized) return;
    LightEvent_Clear(&engine->control_done_event);
    LightLock_Lock(&engine->lock);
    engine->reset_requested = true;
    LightLock_Unlock(&engine->lock);
    LightEvent_Signal(&engine->wake_event);
    LightEvent_Wait(&engine->control_done_event);
}

void audio_engine_set_volume(AudioEngine *engine, uint8_t volume) {
    if (!engine->initialized) return;
    LightLock_Lock(&engine->lock);
    engine->volume = volume;
    engine->volume_dirty = true;
    LightLock_Unlock(&engine->lock);
    LightEvent_Signal(&engine->wake_event);
}

void audio_engine_set_target(AudioEngine *engine, uint16_t target_ms) {
    if (!engine->initialized) return;
    LightLock_Lock(&engine->lock);
    engine->target_ms = target_ms;
    LightLock_Unlock(&engine->lock);
    LightEvent_Signal(&engine->wake_event);
}

void audio_engine_snapshot(AudioEngine *engine, AudioEngineSnapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    if (!engine->initialized) return;
    LightLock_Lock(&engine->lock);
    snapshot->initialized = true;
    snapshot->playing = engine->playing;
    snapshot->bytes_dequeued = engine->bytes_dequeued;
    snapshot->buffers_queued = engine->buffers_queued;
    snapshot->resets = engine->resets;
    snapshot->current_rate = engine->current_rate;
    snapshot->ndsp_sample_pos = ndspChnGetSamplePos(0);
    snapshot->ndsp_frame_count = ndspGetFrameCount();
    snapshot->ndsp_format = ndspChnGetFormat(0);
    snapshot->active_sequence = ndspChnGetWaveBufSeq(0);
    for (size_t i = 0; i < AUDIO_WAVE_BUFFER_COUNT; ++i) {
        snapshot->wave_status[i] = engine->wave_buffers[i].status;
        snapshot->wave_sequence[i] = engine->wave_buffers[i].sequence_id;
    }
    LightLock_Unlock(&engine->lock);
}
