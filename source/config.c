#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_DIR "sdmc:/3ds/3ds-audio-receiver"
#define CONFIG_PATH CONFIG_DIR "/config.ini"
#define CONFIG_TEMP CONFIG_DIR "/config.tmp"
#define CONFIG_BACKUP CONFIG_DIR "/config.bak"
#define CONFIG_VERSION 1

void config_set_defaults(AppConfig *config) {
    config->port = APP_DEFAULT_PORT;
    config->buffer_ms = APP_DEFAULT_BUFFER_MS;
    config->volume = APP_DEFAULT_VOLUME;
    config->language = APP_LANGUAGE_JAPANESE;
}

void config_validate(AppConfig *config) {
    if (config->port < APP_MIN_PORT) config->port = APP_DEFAULT_PORT;
    if (config->buffer_ms < APP_MIN_BUFFER_MS || config->buffer_ms > APP_MAX_BUFFER_MS ||
        config->buffer_ms % APP_BUFFER_STEP_MS != 0) {
        config->buffer_ms = APP_DEFAULT_BUFFER_MS;
    }
    if (config->volume > 100 || config->volume % APP_VOLUME_STEP != 0) {
        config->volume = APP_DEFAULT_VOLUME;
    }
    if (config->language != APP_LANGUAGE_JAPANESE &&
        config->language != APP_LANGUAGE_ENGLISH) {
        config->language = APP_LANGUAGE_JAPANESE;
    }
}

static bool load_path(const char *path, AppConfig *config) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    AppConfig loaded;
    config_set_defaults(&loaded);
    int version = -1;
    char line[96];
    while (fgets(line, sizeof(line), file)) {
        unsigned value;
        if (sscanf(line, "version=%u", &value) == 1) version = (int)value;
        else if (sscanf(line, "port=%u", &value) == 1 && value <= UINT16_MAX)
            loaded.port = (uint16_t)value;
        else if (sscanf(line, "buffer_ms=%u", &value) == 1 && value <= UINT16_MAX)
            loaded.buffer_ms = (uint16_t)value;
        else if (sscanf(line, "volume=%u", &value) == 1 && value <= UINT8_MAX)
            loaded.volume = (uint8_t)value;
        else if (sscanf(line, "language=%u", &value) == 1)
            loaded.language = (AppLanguage)value;
    }
    bool read_ok = !ferror(file);
    fclose(file);
    if (!read_ok || version != CONFIG_VERSION) return false;
    config_validate(&loaded);
    *config = loaded;
    return true;
}

bool config_load(AppConfig *config) {
    config_set_defaults(config);
    if (load_path(CONFIG_PATH, config)) return true;
    return load_path(CONFIG_BACKUP, config);
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s: %s", message, strerror(errno));
}

bool config_save(const AppConfig *config, char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (mkdir("sdmc:/3ds", 0777) != 0 && errno != EEXIST) {
        set_error(error, error_size, "mkdir sdmc:/3ds");
        return false;
    }
    if (mkdir(CONFIG_DIR, 0777) != 0 && errno != EEXIST) {
        set_error(error, error_size, "mkdir config");
        return false;
    }

    FILE *file = fopen(CONFIG_TEMP, "w");
    if (!file) {
        set_error(error, error_size, "open config.tmp");
        return false;
    }
    int result = fprintf(file,
                         "version=%d\nport=%u\nbuffer_ms=%u\nvolume=%u\nlanguage=%u\n",
                         CONFIG_VERSION, config->port, config->buffer_ms, config->volume,
                         (unsigned)config->language);
    bool ok = result > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        set_error(error, error_size, "write config.tmp");
        remove(CONFIG_TEMP);
        return false;
    }

    remove(CONFIG_BACKUP);
    bool had_original = rename(CONFIG_PATH, CONFIG_BACKUP) == 0;
    if (rename(CONFIG_TEMP, CONFIG_PATH) != 0) {
        int saved_errno = errno;
        if (had_original) rename(CONFIG_BACKUP, CONFIG_PATH);
        remove(CONFIG_TEMP);
        errno = saved_errno;
        set_error(error, error_size, "replace config.ini");
        return false;
    }
    remove(CONFIG_BACKUP);
    return true;
}
