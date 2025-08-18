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
#include <pwd.h>
#include <sys/stat.h>
#include "led_config.h"

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
    
    // Visualization
    config->viz_mode = VIZ_MODE_SYSTEM;
    
    // Logging
    config->log_level = LOG_LEVEL_INFO;
    config->enable_debug_logging = 0;
    
    // Performance
    config->enable_adaptive_polling = 1;
    config->cache_system_stats = 1;
    config->max_frame_rate = 10;
    
    // Runtime options
    config->run_as_daemon = 1;
    
    // Statistics
    config->enable_statistics = 0;
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
    // Display settings
    else if (strcmp(key, "min_background_brightness") == 0) {
        config->min_background_brightness = atoi(value);
    } else if (strcmp(key, "max_background_brightness") == 0) {
        config->max_background_brightness = atoi(value);
    } else if (strcmp(key, "min_foreground_brightness") == 0) {
        config->min_foreground_brightness = atoi(value);
    } else if (strcmp(key, "max_foreground_brightness") == 0) {
        config->max_foreground_brightness = atoi(value);
    } else if (strcmp(key, "update_interval_ms") == 0) {
        config->update_interval_ms = atoi(value);
    }
    // Visualization mode
    else if (strcmp(key, "visualization_mode") == 0) {
        if (strcmp(value, "system") == 0) {
            config->viz_mode = VIZ_MODE_SYSTEM;
        } else if (strcmp(value, "cpu_detailed") == 0) {
            config->viz_mode = VIZ_MODE_CPU_DETAILED;
        } else if (strcmp(value, "network_detailed") == 0) {
            config->viz_mode = VIZ_MODE_NETWORK_DETAILED;
        } else if (strcmp(value, "custom") == 0) {
            config->viz_mode = VIZ_MODE_CUSTOM;
        }
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
    // Performance
    else if (strcmp(key, "enable_adaptive_polling") == 0) {
        config->enable_adaptive_polling = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "cache_system_stats") == 0) {
        config->cache_system_stats = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "max_frame_rate") == 0) {
        config->max_frame_rate = atoi(value);
    }
    // Runtime options - pid_file removed, no longer needed
    // Statistics
    else if (strcmp(key, "enable_statistics") == 0) {
        config->enable_statistics = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
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
 * Save current configuration to file in human-readable format
 *
 * DESCRIPTION:
 *   Writes complete LED monitor configuration to specified file path in
 *   key-value format with comments and sections. Converts enum values to
 *   string representations and boolean values to "true"/"false". Creates
 *   well-formatted config file suitable for manual editing. All configuration
 *   parameters are written with descriptive section headers.
 *
 * PARAMETERS:
 *   config - Pointer to led_config_t structure containing values to save
 *   config_path - File path where configuration should be written
 *
 * RETURNS:
 *   LED_SUCCESS if config saved successfully
 *   LED_ERROR_FILE_IO if file cannot be opened for writing
 *
 * SIDE EFFECTS:
 *   Creates or overwrites file at specified path
 *   File I/O operations may affect system state
 *
 * SECURITY FEATURES:
 *   - File creation with default permissions (subject to umask)
 *   - Proper file handle management with explicit close
 *   - No user input directly written to file (all values from validated config)
 *
 * MEMORY MANAGEMENT:
 *   File handle automatically managed; closed before function return
 *
 ****/
led_error_t save_config(const led_config_t* config, const char* config_path) {
    FILE* fp = fopen(config_path, "w");
    if (!fp) {
        return LED_ERROR_FILE_IO;
    }
    
    fprintf(fp, "# LED Monitor Configuration File\n");
    fprintf(fp, "# Auto-generated configuration\n\n");
    
    fprintf(fp, "# Device configuration\n");
    fprintf(fp, "left_device = %s\n", config->left_device_path);
    fprintf(fp, "right_device = %s\n", config->right_device_path);
    fprintf(fp, "\n");
    
    fprintf(fp, "# Display settings\n");
    fprintf(fp, "min_background_brightness = %d\n", config->min_background_brightness);
    fprintf(fp, "max_background_brightness = %d\n", config->max_background_brightness);
    fprintf(fp, "min_foreground_brightness = %d\n", config->min_foreground_brightness);
    fprintf(fp, "max_foreground_brightness = %d\n", config->max_foreground_brightness);
    fprintf(fp, "update_interval_ms = %d\n", config->update_interval_ms);
    fprintf(fp, "\n");
    
    fprintf(fp, "# Visualization mode\n");
    const char* viz_mode_str;
    switch (config->viz_mode) {
        case VIZ_MODE_SYSTEM: viz_mode_str = "system"; break;
        case VIZ_MODE_CPU_DETAILED: viz_mode_str = "cpu_detailed"; break;
        case VIZ_MODE_NETWORK_DETAILED: viz_mode_str = "network_detailed"; break;
        case VIZ_MODE_CUSTOM: viz_mode_str = "custom"; break;
        default: viz_mode_str = "system"; break;
    }
    fprintf(fp, "visualization_mode = %s\n", viz_mode_str);
    fprintf(fp, "\n");
    
    fprintf(fp, "# Logging\n");
    const char* log_level_str;
    switch (config->log_level) {
        case LOG_LEVEL_DEBUG: log_level_str = "debug"; break;
        case LOG_LEVEL_INFO: log_level_str = "info"; break;
        case LOG_LEVEL_WARN: log_level_str = "warn"; break;
        case LOG_LEVEL_ERROR: log_level_str = "error"; break;
        default: log_level_str = "info"; break;
    }
    fprintf(fp, "log_level = %s\n", log_level_str);
    fprintf(fp, "enable_debug_logging = %s\n", config->enable_debug_logging ? "true" : "false");
    fprintf(fp, "\n");
    
    fprintf(fp, "# Performance\n");
    fprintf(fp, "enable_adaptive_polling = %s\n", config->enable_adaptive_polling ? "true" : "false");
    fprintf(fp, "cache_system_stats = %s\n", config->cache_system_stats ? "true" : "false");
    fprintf(fp, "max_frame_rate = %d\n", config->max_frame_rate);
    fprintf(fp, "\n");
    
    fprintf(fp, "# Runtime options\n");
    
    fprintf(fp, "# Statistics\n");
    fprintf(fp, "enable_statistics = %s\n", config->enable_statistics ? "true" : "false");
    
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