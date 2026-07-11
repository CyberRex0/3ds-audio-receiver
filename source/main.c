#include "app_types.h"
#include "audio.h"
#include "config.h"
#include "debug_log.h"
#include "network.h"
#include "localization.h"
#include "ring_buffer.h"
#include "ui.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    AppConfig applied;
    AppConfig staged;
    SharedStream stream;
    NetworkReceiver receiver;
    AudioEngine audio;
    AppUi ui;
    DebugLog debug_log;
    bool network_service_ready;
    char error[192];
    char notice[96];
    uint64_t notice_until;
} Application;

#define UI_STATUS_UPDATE_FRAMES 10

static void capture_debug_sample(Application *app, bool force) {
    uint64_t now = osGetTime();
    if (!app->debug_log.samples || (!force && !debug_log_sample_due(&app->debug_log, now)))
        return;

    DebugSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.elapsed_ms = now - app->debug_log.started_ms;
    struct sockaddr_in source;
    StreamStats stats;
    network_snapshot(&app->stream, &stats, &sample.ring_bytes,
                     &sample.source_active, &source);
    sample.rx_bytes = stats.bytes_received;
    sample.rx_packets = stats.packets_received;
    sample.invalid_packets = stats.invalid_packets;
    sample.foreign_packets = stats.foreign_packets;
    sample.dropped_bytes = stats.dropped_bytes;
    sample.underruns = stats.underruns;
    sample.receiver_running = app->receiver.running;
    AudioEngineSnapshot audio;
    audio_engine_snapshot(&app->audio, &audio);
    sample.playing = audio.playing;
    sample.audio_bytes_dequeued = audio.bytes_dequeued;
    sample.audio_buffers_queued = audio.buffers_queued;
    sample.audio_resets = audio.resets;
    if (audio.initialized) {
        sample.ndsp_rate = audio.current_rate;
        sample.ndsp_format = audio.ndsp_format;
        sample.ndsp_sample_pos = audio.ndsp_sample_pos;
        sample.ndsp_frame_count = audio.ndsp_frame_count;
        sample.active_sequence = audio.active_sequence;
        for (size_t i = 0; i < AUDIO_WAVE_BUFFER_COUNT; ++i) {
            sample.wave_status[i] = audio.wave_status[i];
            sample.wave_sequence[i] = audio.wave_sequence[i];
        }
    }
    debug_log_record(&app->debug_log, &sample, now);
}

static void set_notice(Application *app, const char *message) {
    snprintf(app->notice, sizeof(app->notice), "%s", message);
    app->notice_until = osGetTime() + 2500;
}

static void toggle_debug_logging(Application *app) {
    AppLanguage language = app->staged.language;
    if (app->debug_log.samples) {
        debug_log_destroy(&app->debug_log);
        set_notice(app, loc_get(language, LOC_NOTICE_LOG_DISABLED));
        return;
    }

    if (debug_log_init(&app->debug_log, &app->applied))
        set_notice(app, loc_get(language, LOC_NOTICE_LOG_ENABLED));
    else
        set_notice(app, loc_get(language, LOC_NOTICE_LOG_NO_MEMORY));
}

static bool start_runtime(Application *app) {
    app->error[0] = '\0';
    if (!app->stream.ring.data && !ring_buffer_init(&app->stream.ring, APP_RING_CAPACITY)) {
        snprintf(app->error, sizeof(app->error), "%s",
                 loc_get(app->staged.language, LOC_ERROR_RING_MEMORY));
        return false;
    }
    if (!app->network_service_ready) {
        if (!network_service_init(app->error, sizeof(app->error))) return false;
        app->network_service_ready = true;
    }
    if (!app->audio.initialized &&
        !audio_engine_init(&app->audio, &app->stream, app->applied.buffer_ms,
                           app->applied.volume, app->error, sizeof(app->error))) {
        return false;
    }
    if (!app->receiver.running &&
        !network_receiver_start(&app->receiver, &app->stream, app->applied.port,
                                app->error, sizeof(app->error))) {
        return false;
    }
    return true;
}

static bool apply_settings(Application *app) {
    char save_error[160];
    config_validate(&app->staged);
    if (!config_save(&app->staged, save_error, sizeof(save_error))) {
        snprintf(app->error, sizeof(app->error), "%s", save_error);
        return false;
    }

    bool restart = app->applied.port != app->staged.port ||
                   app->applied.buffer_ms != app->staged.buffer_ms;
    if (restart) {
        network_receiver_stop(&app->receiver);
        audio_engine_reset(&app->audio);
        network_forget_source(&app->stream);
    }
    app->applied = app->staged;
    app->debug_log.config = app->applied;
    audio_engine_set_target(&app->audio, app->applied.buffer_ms);
    audio_engine_set_volume(&app->audio, app->applied.volume);
    app->error[0] = '\0';

    if (restart &&
        !network_receiver_start(&app->receiver, &app->stream, app->applied.port,
                                app->error, sizeof(app->error))) {
        return false;
    }
    set_notice(app, loc_get(app->staged.language, LOC_NOTICE_SETTINGS_SAVED));
    return true;
}

static void change_selected_setting(Application *app, int direction) {
    switch (app->ui.selected_setting) {
        case 0: {
            int value = (int)app->staged.port + direction;
            if (value < APP_MIN_PORT) value = APP_MIN_PORT;
            if (value > APP_MAX_PORT) value = APP_MAX_PORT;
            app->staged.port = (uint16_t)value;
            break;
        }
        case 1: {
            int value = (int)app->staged.buffer_ms + direction * APP_BUFFER_STEP_MS;
            if (value < APP_MIN_BUFFER_MS) value = APP_MIN_BUFFER_MS;
            if (value > APP_MAX_BUFFER_MS) value = APP_MAX_BUFFER_MS;
            app->staged.buffer_ms = (uint16_t)value;
            break;
        }
        case 2: {
            int value = (int)app->staged.volume + direction * APP_VOLUME_STEP;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            app->staged.volume = (uint8_t)value;
            break;
        }
        case 3:
            app->staged.language = app->staged.language == APP_LANGUAGE_JAPANESE
                                       ? APP_LANGUAGE_ENGLISH
                                       : APP_LANGUAGE_JAPANESE;
            break;
    }
}

static void handle_input(Application *app, u32 keys) {
    if (keys & KEY_UP) {
        app->ui.selected_setting = (app->ui.selected_setting + 3) % 4;
    }
    if (keys & KEY_DOWN) {
        app->ui.selected_setting = (app->ui.selected_setting + 1) % 4;
    }
    if (keys & KEY_LEFT) change_selected_setting(app, -1);
    if (keys & KEY_RIGHT) change_selected_setting(app, 1);
    if (app->ui.selected_setting == 0 && (keys & KEY_L)) {
        int value = (int)app->staged.port - 100;
        app->staged.port = (uint16_t)(value < APP_MIN_PORT ? APP_MIN_PORT : value);
    }
    if (app->ui.selected_setting == 0 && (keys & KEY_R)) {
        int value = (int)app->staged.port + 100;
        app->staged.port = (uint16_t)(value > APP_MAX_PORT ? APP_MAX_PORT : value);
    }
    if (keys & KEY_X) {
        audio_engine_reset(&app->audio);
        network_forget_source(&app->stream);
        set_notice(app, loc_get(app->staged.language, LOC_NOTICE_SOURCE_DISCONNECTED));
    }
    if (keys & KEY_Y) toggle_debug_logging(app);
    if (keys & KEY_SELECT) {
        app->staged.language = app->staged.language == APP_LANGUAGE_JAPANESE
                                   ? APP_LANGUAGE_ENGLISH
                                   : APP_LANGUAGE_JAPANESE;
        set_notice(app, loc_get(app->staged.language,
                                app->staged.language == APP_LANGUAGE_JAPANESE
                                    ? LOC_NOTICE_LANGUAGE_JAPANESE
                                    : LOC_NOTICE_LANGUAGE_ENGLISH));
    }
    if (keys & KEY_A) {
        if (app->error[0]) {
            if (!app_config_equal(&app->applied, &app->staged))
                apply_settings(app);
            else
                start_runtime(app);
        } else if (!app_config_equal(&app->applied, &app->staged)) {
            apply_settings(app);
        }
    }
}

static void build_view_model(Application *app, UiViewModel *view) {
    memset(view, 0, sizeof(*view));
    view->applied = app->applied;
    view->staged = app->staged;
    AudioEngineSnapshot audio;
    audio_engine_snapshot(&app->audio, &audio);
    view->playing = audio.playing;
    view->receiver_running = app->receiver.running;
    view->error = app->error;
    view->notice = osGetTime() < app->notice_until ? app->notice : "";
    view->debug_logging = app->debug_log.samples != NULL;

    struct in_addr local_address = {
        .s_addr = app->network_service_ready ? (in_addr_t)gethostid() : 0
    };
    if (!app->network_service_ready ||
        !inet_ntop(AF_INET, &local_address, view->local_ip, sizeof(view->local_ip)))
        snprintf(view->local_ip, sizeof(view->local_ip), "0.0.0.0");

    struct sockaddr_in source;
    network_snapshot(&app->stream, &view->stats, &view->buffered_bytes,
                     &view->source_active, &source);
    if (view->source_active) {
        if (!inet_ntop(AF_INET, &source.sin_addr, view->source_ip, sizeof(view->source_ip)))
            snprintf(view->source_ip, sizeof(view->source_ip), "?");
    }
}

int main(void) {
    Application app;
    memset(&app, 0, sizeof(app));
    app.receiver.socket_fd = -1;

    gfxInitDefault();
    if (!ui_init(&app.ui)) {
        gfxExit();
        return 1;
    }

    LightLock_Init(&app.stream.lock);
    if (!ring_buffer_init(&app.stream.ring, APP_RING_CAPACITY)) {
        snprintf(app.error, sizeof(app.error), "%s",
                 loc_get(app.staged.language, LOC_ERROR_RING_MEMORY));
    }
    config_load(&app.applied);
    app.staged = app.applied;
    if (!app.error[0]) start_runtime(&app);

    UiViewModel view;
    build_view_model(&app, &view);
    unsigned int status_update_frame = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        handle_input(&app, keys);
        capture_debug_sample(&app, false);

        status_update_frame++;
        if (status_update_frame >= UI_STATUS_UPDATE_FRAMES) {
            build_view_model(&app, &view);
            status_update_frame = 0;
        }
        ui_render(&app.ui, &view);
    }

    capture_debug_sample(&app, true);
    network_receiver_stop(&app.receiver);
    if (app.debug_log.samples) {
        char debug_error[128];
        debug_log_save(&app.debug_log, debug_error, sizeof(debug_error));
    }
    audio_engine_exit(&app.audio);
    if (app.network_service_ready) network_service_exit();
    ring_buffer_destroy(&app.stream.ring);
    debug_log_destroy(&app.debug_log);
    ui_exit(&app.ui);
    gfxExit();
    return 0;
}
