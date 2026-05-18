/*
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LED_CONFIG_H
#define LED_CONFIG_H

#include "led_errors.h"

#define MAX_DEVICE_PATH 64
#define MAX_CONFIG_LINE 256
#define DEFAULT_CONFIG_PATH "/etc/led_monitor.conf"
#define USER_CONFIG_PATH "~/.config/led_monitor.conf"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

typedef struct {
    // Device configuration
    char left_device_path[MAX_DEVICE_PATH];
    char right_device_path[MAX_DEVICE_PATH];

    // Display settings
    int min_background_brightness;
    int max_background_brightness;
    int min_foreground_brightness;
    int max_foreground_brightness;
    int update_interval_ms;

    // Logging
    log_level_t log_level;
    int enable_debug_logging;

    // Runtime options
    int run_as_daemon;
} led_config_t;

// Configuration management
led_error_t load_config(led_config_t* config, const char* config_path);
void init_default_config(led_config_t* config);
led_error_t reload_config(led_config_t* config);

#endif // LED_CONFIG_H