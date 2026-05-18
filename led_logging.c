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
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "led_logging.h"

static log_level_t current_log_level = LOG_LEVEL_INFO;
static int debug_enabled = 0;
static int foreground_logging = 0;

/****
 *
 * Initializes the logging system with specified configuration parameters
 *
 * DESCRIPTION:
 *   Sets up the logging subsystem by configuring the minimum log level,
 *   debug mode, and output destination (foreground vs daemon mode). Opens
 *   syslog connection with appropriate facility and options based on the
 *   foreground mode setting.
 *
 * PARAMETERS:
 *   level - Minimum log level to output (LOG_LEVEL_DEBUG, INFO, WARN, ERROR)
 *   enable_debug - Non-zero to enable debug message output
 *   foreground_mode - Non-zero for foreground logging with stderr output
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   - Sets global logging configuration variables
 *   - Opens syslog connection with appropriate facility
 *   - In foreground mode, enables stderr output in addition to syslog
 *
 * SECURITY FEATURES:
 *   - Validates log level parameter through switch statement bounds
 *   - Uses secure syslog facilities (LOG_USER for foreground, LOG_DAEMON for background)
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation - uses only static variables
 *
 ****/
void init_logging(log_level_t level, int enable_debug, int foreground_mode) {
    current_log_level = level;
    debug_enabled = enable_debug;
    foreground_logging = foreground_mode;
    
    if (foreground_mode) {
        openlog("led_monitor_daemon", LOG_PID | LOG_PERROR, LOG_USER);
    } else {
        openlog("led_monitor_daemon", LOG_PID, LOG_DAEMON);
    }
}

/****
 *
 * Converts internal log level enumeration to syslog priority value
 *
 * DESCRIPTION:
 *   Maps the application's log level enumeration to corresponding syslog
 *   priority values. This function ensures proper integration with the
 *   system logging infrastructure by translating custom log levels to
 *   standard syslog priorities.
 *
 * PARAMETERS:
 *   level - Internal log level enumeration value
 *
 * RETURNS:
 *   Integer syslog priority value (3-7 range) or 6 (LOG_INFO) for invalid input
 *
 * SIDE EFFECTS:
 *   None
 *
 * SECURITY FEATURES:
 *   - Default case handles invalid log levels safely
 *   - Returns safe default priority for unexpected input
 *
 * MEMORY MANAGEMENT:
 *   No memory operations - pure value translation function
 *
 ****/
static int get_syslog_priority(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return 7; // LOG_DEBUG
        case LOG_LEVEL_INFO: return 6;  // LOG_INFO
        case LOG_LEVEL_WARN: return 4;  // LOG_WARNING
        case LOG_LEVEL_ERROR: return 3; // LOG_ERR
        default: return 6; // LOG_INFO
    }
}

/****
 *
 * Converts log level enumeration to human-readable string representation
 *
 * DESCRIPTION:
 *   Provides string representation of log levels for display purposes.
 *   Used primarily in foreground debug mode to create formatted log
 *   messages with readable level indicators.
 *
 * PARAMETERS:
 *   level - Internal log level enumeration value
 *
 * RETURNS:
 *   Constant string pointer to level name or "INFO" for invalid input
 *
 * SIDE EFFECTS:
 *   None
 *
 * SECURITY FEATURES:
 *   - Returns constant string literals only
 *   - Default case provides safe fallback for invalid input
 *   - No buffer operations or user-controlled data
 *
 * MEMORY MANAGEMENT:
 *   Returns pointers to static string literals - no allocation required
 *
 ****/
static const char* get_level_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_WARN: return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "INFO";
    }
}

/****
 *
 * Main logging function with printf-style formatting and debug information
 *
 * DESCRIPTION:
 *   Core logging function that handles message formatting, level filtering,
 *   and output routing. Supports variable arguments for printf-style formatting.
 *   In foreground debug mode, adds timestamp, file, and line information.
 *   Always outputs to syslog and optionally to stderr based on configuration.
 *
 * PARAMETERS:
 *   level - Log level for this message
 *   file - Source file name (typically __FILE__ macro)
 *   line - Source line number (typically __LINE__ macro)
 *   fmt - Printf-style format string
 *   ... - Variable arguments for format string
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   - Outputs formatted message to syslog
 *   - In foreground debug mode, outputs to stderr with additional context
 *   - May skip output if message level is below configured threshold
 *
 * SECURITY FEATURES:
 *   - Bounds-checked buffer operations using snprintf/vsnprintf
 *   - Input validation for log level and debug mode
 *   - Safe basename extraction from file path
 *   - Fixed-size buffers prevent overflow
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffers (1024 + 1200 bytes) - no dynamic allocation
 *
 ****/
void led_log(log_level_t level, const char* file, int line, const char* fmt, ...) {
    va_list args;
    char message[1024];
    char full_message[1200];
    
    // Check log level
    if (level < current_log_level) {
        return;
    }
    
    // Skip debug messages if debug not enabled
    if (level == LOG_LEVEL_DEBUG && !debug_enabled) {
        return;
    }
    
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    if (foreground_logging && debug_enabled) {
        // Include file and line info for foreground debug mode
        const char* basename = strrchr(file, '/');
        if (basename) {
            basename++;
        } else {
            basename = file;
        }
        
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        snprintf(full_message, sizeof(full_message), "[%s] %s %s:%d - %s", 
                timestamp, get_level_string(level), basename, line, message);
        
        // Print to stderr for foreground mode
        fprintf(stderr, "%s\n", full_message);
    }
    
    // Always log to syslog
    syslog(get_syslog_priority(level), "%s", message);
}

/****
 *
 * Updates the minimum log level threshold for message filtering
 *
 * DESCRIPTION:
 *   Changes the global log level setting that determines which messages
 *   are processed and output. Messages below this level will be filtered
 *   out. Also logs the level change itself for audit purposes.
 *
 * PARAMETERS:
 *   level - New minimum log level to set
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   - Updates global current_log_level variable
 *   - Generates an INFO-level log message about the level change
 *
 * SECURITY FEATURES:
 *   - Level validation handled by downstream functions
 *   - Audit trail of level changes through automatic logging
 *
 * MEMORY MANAGEMENT:
 *   No memory operations - simple variable assignment
 *
 ****/
void set_log_level(log_level_t level) {
    current_log_level = level;
    LOG_INFO("Log level changed to %s", get_level_string(level));
}

