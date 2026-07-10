#pragma once

#include "app_types.h"

#include <citro2d.h>

typedef struct {
    C3D_RenderTarget *top;
    C3D_RenderTarget *bottom;
    C2D_TextBuf text_buffer;
    int selected_setting;
} AppUi;

typedef struct {
    char local_ip[INET_ADDRSTRLEN];
    char source_ip[INET_ADDRSTRLEN];
    bool source_active;
    bool playing;
    bool receiver_running;
    size_t buffered_bytes;
    StreamStats stats;
    AppConfig applied;
    AppConfig staged;
    const char *error;
    const char *notice;
    bool debug_logging;
} UiViewModel;

bool ui_init(AppUi *ui);
void ui_exit(AppUi *ui);
void ui_render(AppUi *ui, const UiViewModel *view);
