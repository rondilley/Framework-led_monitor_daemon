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

#ifndef LED_LOGGING_H
#define LED_LOGGING_H

#include "led_config.h"

// Initialize logging system
void init_logging(log_level_t level, int enable_debug, int foreground_mode);

// Logging macros - avoid conflicts with syslog
#define LED_LOG_DEBUG(fmt, ...) led_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LED_LOG_INFO(fmt, ...) led_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LED_LOG_WARN(fmt, ...) led_log(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LED_LOG_ERROR(fmt, ...) led_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Convenience macros that override syslog ones
#ifndef LED_DISABLE_LOG_REDEFINITION
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_INFO  
#undef LOG_INFO
#endif
#define LOG_DEBUG LED_LOG_DEBUG
#define LOG_INFO LED_LOG_INFO
#define LOG_WARN LED_LOG_WARN
#define LOG_ERROR LED_LOG_ERROR
#endif

// Core logging function
void led_log(log_level_t level, const char* file, int line, const char* fmt, ...);

// Runtime log level adjustment
void set_log_level(log_level_t level);

#endif // LED_LOGGING_H