#pragma once

#include "app_types.h"

void config_set_defaults(AppConfig *config);
void config_validate(AppConfig *config);
bool config_load(AppConfig *config);
bool config_save(const AppConfig *config, char *error, size_t error_size);
