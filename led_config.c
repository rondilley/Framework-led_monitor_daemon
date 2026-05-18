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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <syslog.h>
#include <sys/stat.h>
#include "led_config.h"

// Strict integer parser: succeeds only when the whole string is a base-10
// integer within [lo, hi]. Replaces atoi(), which silently returned 0 for
// garbage (e.g. "abc") and propagated negatives into unsigned brightness
// math and the usleep() argument.
static int parse_int_range(const char *s, int lo, int hi, int *out) {
    if (!s || !*s) return 0;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

// Apply parse_int_range and log a warning (keeping the existing default)
// if the value is malformed or out of range. Called from config parsing,
// which may run before init_logging — syslog() opens implicitly in that
// case, which is fine for a startup warning.
static void apply_int(const char *key, const char *value, int lo, int hi, int *field) {
    int v;
    if (parse_int_range(value, lo, hi, &v)) {
        *field = v;
    } else {
        syslog(LOG_WARNING,
               "config: ignoring '%s = %s' (not an integer in [%d, %d]); keeping default %d",
               key, value, lo, hi, *field);
    }
}

/****
 *
 * Initialize LED monitor configuration structure with safe default values
 *
 * DESCRIPTION:
 *   Populates a led_config_t structure with sensible default values for all
 *   configuration parameters. Sets device paths for auto-detection, brightness
 *   ranges for adaptive display, standard update intervals, and enables
 *   performance optimizations. This ensures the system has valid configuration
 *   values even when no config file is present.
 *
 * PARAMETERS:
 *   config - Pointer to led_config_t structure to initialize
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Modifies all fields in the provided config structure
 *
 * SECURITY FEATURES:
 *   - No bounds checking needed as all values are compile-time constants
 *   - Safe string operations using strcpy with known string literals
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation; operates on caller-provided structure
 *
 ****/
void init_default_config(led_config_t* config) {
    // Device paths (auto-detect by default)
    strcpy(config->left_device_path, "4.2");  // USB path, not full device
    strcpy(config->right_device_path, "3.3");

    // Display settings
    config->min_background_brightness = 12;
    config->max_background_brightness = 35;
    config->min_foreground_brightness = 24;
    config->max_foreground_brightness = 160;
    config->update_interval_ms = 100;

    // Logging
    config->log_level = LOG_LEVEL_INFO;
    config->enable_debug_logging = 0;

    // Runtime options
    config->run_as_daemon = 1;
}

/****
 *
 * Parse a single line from configuration file and update config structure
 *
 * DESCRIPTION:
 *   Parses key-value pairs from configuration file lines using format "key = value".
 *   Supports device paths, display settings, visualization modes, logging levels,
 *   performance options, and runtime settings. Handles type conversion for
 *   integers and boolean values. Ignores comments (#), empty lines, and
 *   malformed entries for robustness.
 *
 * PARAMETERS:
 *   config - Pointer to led_config_t structure to update
 *   line - Configuration line string to parse
 *
 * RETURNS:
 *   LED_SUCCESS on successful parsing or benign skip conditions
 *
 * SIDE EFFECTS:
 *   Modifies corresponding fields in config structure based on parsed key
 *
 * SECURITY FEATURES:
 *   - Buffer overflow protection with bounded sscanf and strncpy
 *   - Null termination enforcement for string fields
 *   - Input validation through key comparison and enum mapping
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffers for parsing; no dynamic allocation
 *
 ****/
static led_error_t parse_config_line(led_config_t* config, const char* line) {
    char key[64], value[192];
    
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
        return LED_SUCCESS;
    }
    
    if (sscanf(line, "%63s = %191s", key, value) != 2) {
        return LED_SUCCESS; // Skip malformed lines
    }
    
    // Device configuration
    if (strcmp(key, "left_device") == 0) {
        strncpy(config->left_device_path, value, sizeof(config->left_device_path) - 1);
        config->left_device_path[sizeof(config->left_device_path) - 1] = '\0';
    } else if (strcmp(key, "right_device") == 0) {
        strncpy(config->right_device_path, value, sizeof(config->right_device_path) - 1);
        config->right_device_path[sizeof(config->right_device_path) - 1] = '\0';
    }
    // Display settings (all brightnesses are 0-255; update interval is
    // bounded to avoid busy loops at 0 and absurd waits past a minute).
    else if (strcmp(key, "min_background_brightness") == 0) {
        apply_int(key, value, 0, 255, &config->min_background_brightness);
    } else if (strcmp(key, "max_background_brightness") == 0) {
        apply_int(key, value, 0, 255, &config->max_background_brightness);
    } else if (strcmp(key, "min_foreground_brightness") == 0) {
        apply_int(key, value, 0, 255, &config->min_foreground_brightness);
    } else if (strcmp(key, "max_foreground_brightness") == 0) {
        apply_int(key, value, 0, 255, &config->max_foreground_brightness);
    } else if (strcmp(key, "update_interval_ms") == 0) {
        apply_int(key, value, 10, 60000, &config->update_interval_ms);
    }
    // Logging
    else if (strcmp(key, "log_level") == 0) {
        if (strcmp(value, "debug") == 0) {
            config->log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(value, "info") == 0) {
            config->log_level = LOG_LEVEL_INFO;
        } else if (strcmp(value, "warn") == 0) {
            config->log_level = LOG_LEVEL_WARN;
        } else if (strcmp(value, "error") == 0) {
            config->log_level = LOG_LEVEL_ERROR;
        }
    } else if (strcmp(key, "enable_debug_logging") == 0) {
        config->enable_debug_logging = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }

    return LED_SUCCESS;
}

/****
 *
 * Load configuration from file with fallback to user-specific config
 *
 * DESCRIPTION:
 *   Loads LED monitor configuration from specified file path. Initializes
 *   config with defaults first, then parses config file line by line.
 *   Implements fallback mechanism: if system config fails and path matches
 *   DEFAULT_CONFIG_PATH, attempts to load from user's home directory
 *   (~/.config/led_monitor.conf). Gracefully handles missing files by
 *   using default configuration.
 *
 * PARAMETERS:
 *   config - Pointer to led_config_t structure to populate
 *   config_path - Path to configuration file to load
 *
 * RETURNS:
 *   LED_SUCCESS if config loaded successfully
 *   LED_ERROR_FILE_IO if config file cannot be opened
 *
 * SIDE EFFECTS:
 *   Modifies config structure fields based on file contents
 *   Opens and closes file handles
 *
 * SECURITY FEATURES:
 *   - Safe file path handling with bounded string operations
 *   - getpwuid() used to safely retrieve user home directory
 *   - Proper file handle cleanup in all code paths
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffers for file I/O; automatic cleanup on return
 *
 ****/
led_error_t load_config(led_config_t* config, const char* config_path) {
    FILE* fp;
    char line[MAX_CONFIG_LINE];
    
    // Initialize with defaults first
    init_default_config(config);
    
    // Try to open config file
    fp = fopen(config_path, "r");
    if (!fp) {
        // Try user config path if system config fails
        if (strcmp(config_path, DEFAULT_CONFIG_PATH) == 0) {
            char user_config[256];
            struct passwd* pw = getpwuid(getuid());
            if (pw) {
                snprintf(user_config, sizeof(user_config), "%s/.config/led_monitor.conf", pw->pw_dir);
                fp = fopen(user_config, "r");
            }
        }
        
        if (!fp) {
            return LED_ERROR_FILE_IO; // Config file not found, use defaults
        }
    }
    
    // Parse configuration file
    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';
        parse_config_line(config, line);
    }
    
    fclose(fp);
    return LED_SUCCESS;
}

/****
 *
 * Reload configuration using standard system and user config paths
 *
 * DESCRIPTION:
 *   Convenience function that attempts to reload configuration from standard
 *   locations. First tries system-wide configuration at DEFAULT_CONFIG_PATH,
 *   then falls back to user-specific config in ~/.config/led_monitor.conf
 *   if system config fails. Implements the standard configuration hierarchy
 *   for Unix-like systems.
 *
 * PARAMETERS:
 *   config - Pointer to led_config_t structure to update with reloaded values
 *
 * RETURNS:
 *   LED_SUCCESS if any config file was loaded successfully
 *   LED_ERROR_FILE_IO if no config files could be found or opened
 *
 * SIDE EFFECTS:
 *   Modifies config structure with values from found configuration file
 *   May perform file system access to locate user home directory
 *
 * SECURITY FEATURES:
 *   - Uses getpwuid() to safely retrieve current user information
 *   - Bounded string operations for path construction
 *   - Inherits security features from load_config() function
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffer for user config path construction
 *
 ****/
led_error_t reload_config(led_config_t* config) {
    // Try system config first, then user config
    led_error_t result = load_config(config, DEFAULT_CONFIG_PATH);
    if (result != LED_SUCCESS) {
        char user_config[256];
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            snprintf(user_config, sizeof(user_config), "%s/.config/led_monitor.conf", pw->pw_dir);
            result = load_config(config, user_config);
        }
    }
    return result;
}