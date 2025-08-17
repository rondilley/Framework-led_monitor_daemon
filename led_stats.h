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

#ifndef LED_STATS_H
#define LED_STATS_H

#include <time.h>
#include <stdint.h>

typedef struct {
    // Frame statistics
    uint64_t frames_sent;
    uint64_t frames_dropped;
    uint64_t communication_errors;
    
    // Performance metrics
    double avg_frame_time_ms;
    double max_frame_time_ms;
    uint32_t current_fps;
    
    // Device status
    int left_device_connected;
    int right_device_connected;
    time_t last_communication;
    
    // System resource usage
    double cpu_usage_percent;
    uint64_t memory_usage_bytes;
    
    // Uptime
    time_t start_time;
    uint64_t uptime_seconds;
    
    // Error counters
    uint32_t device_reconnects;
    uint32_t config_reloads;
    uint32_t thread_restarts;
} led_statistics_t;

// Statistics management
void init_statistics(void);
void update_frame_stats(double frame_time_ms, int success);
void update_device_status(int left_connected, int right_connected);
void update_error_count(const char* error_type);
void reset_statistics(void);

// Statistics access
const led_statistics_t* get_statistics(void);
void format_statistics_json(char* buffer, size_t buffer_size);
void format_statistics_text(char* buffer, size_t buffer_size);

// HTTP statistics server (if enabled)
int start_stats_server(int port);
void stop_stats_server(void);

#endif // LED_STATS_H