#include "ui.h"
#include "localization.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define COLOR_BACKGROUND C2D_Color32(17, 24, 39, 255)
#define COLOR_PANEL C2D_Color32(30, 41, 59, 255)
#define COLOR_ACCENT C2D_Color32(56, 189, 248, 255)
#define COLOR_TEXT C2D_Color32(241, 245, 249, 255)
#define COLOR_MUTED C2D_Color32(148, 163, 184, 255)
#define COLOR_GOOD C2D_Color32(74, 222, 128, 255)
#define COLOR_WARN C2D_Color32(251, 191, 36, 255)
#define COLOR_ERROR C2D_Color32(248, 113, 113, 255)

static void draw_text_v(AppUi *ui, float x, float y, float z, float scale, u32 color,
                        const char *format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    C2D_Text text;
    C2D_TextParse(&text, ui->text_buffer, buffer);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, z, scale, scale, color);
}

static void draw_text(AppUi *ui, float x, float y, float scale, u32 color,
                      const char *format, ...) {
    va_list args;
    va_start(args, format);
    draw_text_v(ui, x, y, 0.5f, scale, color, format, args);
    va_end(args);
}

static void draw_overlay_text(AppUi *ui, float x, float y, float scale, u32 color,
                              const char *format, ...) {
    va_list args;
    va_start(args, format);
    draw_text_v(ui, x, y, 0.9f, scale, color, format, args);
    va_end(args);
}

bool ui_init(AppUi *ui) {
    memset(ui, 0, sizeof(*ui));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }
    C2D_Prepare();
    ui->top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    ui->bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui->text_buffer = C2D_TextBufNew(4096);
    if (!ui->top || !ui->bottom || !ui->text_buffer) {
        ui_exit(ui);
        return false;
    }
    return true;
}

void ui_exit(AppUi *ui) {
    if (ui->text_buffer) C2D_TextBufDelete(ui->text_buffer);
    C2D_Fini();
    C3D_Fini();
    memset(ui, 0, sizeof(*ui));
}

static void render_top(AppUi *ui, const UiViewModel *view) {
    AppLanguage language = view->staged.language;
    C2D_TargetClear(ui->top, COLOR_BACKGROUND);
    C2D_SceneBegin(ui->top);
    C2D_DrawRectSolid(0, 0, 0, 400, 38, COLOR_PANEL);
    draw_text(ui, 16, 10, 0.65f, COLOR_TEXT, "3DS Audio Receiver");

    const char *status = loc_get(language, LOC_STATUS_IDLE);
    u32 status_color = COLOR_MUTED;
    if (view->error && view->error[0]) {
        status = loc_get(language, LOC_STATUS_ERROR);
        status_color = COLOR_ERROR;
    } else if (view->playing) {
        status = loc_get(language, LOC_STATUS_PLAYING);
        status_color = COLOR_GOOD;
    } else if (view->source_active) {
        status = loc_get(language, LOC_STATUS_BUFFERING);
        status_color = COLOR_WARN;
    } else if (!view->receiver_running) {
        status = loc_get(language, LOC_STATUS_STOPPED);
        status_color = COLOR_ERROR;
    }

    draw_text(ui, 16, 52, 0.56f, COLOR_MUTED, "%s", loc_get(language, LOC_STATE));
    draw_text(ui, 116, 52, 0.62f, status_color, "%s", status);
    draw_text(ui, 16, 76, 0.56f, COLOR_MUTED, "%s", loc_get(language, LOC_LISTENING_ON));
    draw_text(ui, 116, 76, 0.56f, COLOR_TEXT, "%s:%u", view->local_ip,
              view->applied.port);
    draw_text(ui, 16, 100, 0.56f, COLOR_MUTED, "%s", loc_get(language, LOC_SOURCE));
    draw_text(ui, 116, 100, 0.56f, COLOR_TEXT, "%s",
              view->source_active ? view->source_ip : loc_get(language, LOC_NOT_CONNECTED));

    float buffered_ms = (float)view->buffered_bytes * 1000.0f /
                        (APP_SAMPLE_RATE * APP_FRAME_BYTES);
    draw_text(ui, 16, 132, 0.52f, COLOR_MUTED, loc_get(language, LOC_BUFFER_FORMAT), buffered_ms,
              view->applied.buffer_ms);
    float ratio = buffered_ms / view->applied.buffer_ms;
    if (ratio > 1.0f) ratio = 1.0f;
    C2D_DrawRectSolid(16, 151, 0, 368, 8, COLOR_PANEL);
    C2D_DrawRectSolid(16, 151, 0.1f, 368 * ratio, 8,
                      view->playing ? COLOR_GOOD : COLOR_ACCENT);

    draw_text(ui, 16, 176, 0.48f, COLOR_MUTED, loc_get(language, LOC_RECEIVED_FORMAT),
              (unsigned long long)view->stats.packets_received,
              (unsigned long long)(view->stats.bytes_received / 1024));
    draw_text(ui, 16, 196, 0.48f, COLOR_MUTED, loc_get(language, LOC_DROPPED_FORMAT),
              (unsigned long long)(view->stats.dropped_bytes / 1024),
              (unsigned long long)view->stats.invalid_packets,
              (unsigned long long)view->stats.foreign_packets);
    draw_text(ui, 16, 216, 0.48f, COLOR_MUTED, loc_get(language, LOC_UNDERRUNS_FORMAT),
              (unsigned long)view->stats.underruns);

    if (view->error && view->error[0]) {
        C2D_DrawRectSolid(8, 40, 0.8f, 384, 190, C2D_Color32(50, 20, 28, 245));
        draw_overlay_text(ui, 20, 58, 0.58f, COLOR_ERROR, "%s", loc_get(language, LOC_ERROR_TITLE));
        draw_overlay_text(ui, 20, 91, 0.46f, COLOR_TEXT, "%s", view->error);
        draw_overlay_text(ui, 20, 185, 0.50f, COLOR_TEXT,
                          "%s", loc_get(language, LOC_ERROR_ACTIONS));
    }
}

static void setting_row(AppUi *ui, int index, float y, const char *name, const char *value) {
    bool selected = ui->selected_setting == index;
    if (selected) C2D_DrawRectSolid(10, y - 5, 0, 300, 27, COLOR_PANEL);
    draw_text(ui, 20, y, 0.52f, selected ? COLOR_ACCENT : COLOR_MUTED, "%s", name);
    draw_text(ui, 180, y, 0.55f, COLOR_TEXT, "%s", value);
}

static void render_bottom(AppUi *ui, const UiViewModel *view) {
    AppLanguage language = view->staged.language;
    C2D_TargetClear(ui->bottom, COLOR_BACKGROUND);
    C2D_SceneBegin(ui->bottom);
    draw_text(ui, 14, 8, 0.62f, COLOR_TEXT, "%s", loc_get(language, LOC_SETTINGS_TITLE));

    char value[48];
    snprintf(value, sizeof(value), "%u", view->staged.port);
    setting_row(ui, 0, 39, loc_get(language, LOC_UDP_PORT), value);
    snprintf(value, sizeof(value), "%u ms", view->staged.buffer_ms);
    setting_row(ui, 1, 68, loc_get(language, LOC_BUFFER), value);
    snprintf(value, sizeof(value), "%u %%", view->staged.volume);
    setting_row(ui, 2, 97, loc_get(language, LOC_VOLUME), value);
    setting_row(ui, 3, 126, loc_get(language, LOC_LANGUAGE),
                language == APP_LANGUAGE_ENGLISH ? "English" : "日本語");

    bool changed = !app_config_equal(&view->applied, &view->staged);
    draw_text(ui, 14, 151, 0.43f, changed ? COLOR_WARN : COLOR_MUTED, "%s",
              loc_get(language, changed ? LOC_UNSAVED_CHANGES : LOC_SETTINGS_APPLIED));
    if (view->notice && view->notice[0])
        draw_text(ui, 14, 168, 0.40f, COLOR_GOOD, "%s", view->notice);
    else
        draw_text(ui, 14, 168, 0.38f, view->debug_logging ? COLOR_WARN : COLOR_MUTED,
                  "%s", loc_get(language, view->debug_logging ? LOC_LOG_ON : LOC_LOG_OFF));

    draw_text(ui, 14, 190, 0.37f, COLOR_TEXT, "%s", loc_get(language, LOC_CONTROLS_SELECT_CHANGE));
    draw_text(ui, 14, 207, 0.37f, COLOR_TEXT, "%s", loc_get(language, LOC_CONTROLS_ACTIONS));
    draw_text(ui, 14, 224, 0.37f, COLOR_TEXT, "%s", loc_get(language, LOC_CONTROLS_SYSTEM));
}

void ui_render(AppUi *ui, const UiViewModel *view) {
    C2D_TextBufClear(ui->text_buffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    render_top(ui, view);
    render_bottom(ui, view);
    C3D_FrameEnd(0);
}
