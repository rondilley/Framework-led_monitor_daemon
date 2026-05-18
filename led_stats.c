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

#include <string.h>
#include <time.h>
#include <pthread.h>
#include "led_stats.h"
#include "led_logging.h"

static led_statistics_t stats;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/****
 *
 * Initializes the statistics collection system
 *
 * DESCRIPTION:
 *   Initializes the global statistics structure with zero values and sets
 *   the start time to the current system time. This function should be called
 *   once during daemon startup to prepare the statistics tracking system.
 *   All counters and timing metrics are reset to their initial state.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Initializes global statistics structure
 *   - Acquires and releases the statistics mutex
 *   - Logs initialization message
 *   - Sets start_time to current system time
 *
 * SECURITY FEATURES:
 *   - Thread-safe initialization using mutex protection
 *   - Zeroes sensitive data structures using memset
 *
 * MEMORY MANAGEMENT:
 *   Uses global static storage for statistics. No dynamic allocation.
 *
 ****/
void init_statistics(void) {
    pthread_mutex_lock(&stats_mutex);
    
    memset(&stats, 0, sizeof(stats));
    stats.start_time = time(NULL);
    stats.max_frame_time_ms = 0.0;
    stats.avg_frame_time_ms = 0.0;
    
    pthread_mutex_unlock(&stats_mutex);
    
    LOG_INFO("Statistics system initialized");
}

/****
 *
 * Updates frame timing and success/failure statistics
 *
 * DESCRIPTION:
 *   Records frame rendering statistics including timing metrics and success/failure
 *   counts. Uses exponential moving average for frame timing to provide smooth
 *   performance metrics. Calculates current FPS based on average frame time.
 *   Tracks both successful frame transmissions and dropped frames for monitoring.
 *
 * PARAMETERS:
 *   frame_time_ms - Time taken to render and send the frame in milliseconds
 *   success - Non-zero if frame was sent successfully, zero if dropped
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Updates global frame statistics counters
 *   - Modifies average and maximum frame timing metrics
 *   - Updates last communication timestamp
 *   - Recalculates current FPS estimate
 *
 * SECURITY FEATURES:
 *   - Thread-safe updates using mutex protection
 *   - Bounds checking on frame_time_ms for positive values
 *   - Safe division operations with zero checks
 *
 * MEMORY MANAGEMENT:
 *   Updates global static storage only. No dynamic allocation.
 *
 ****/
void update_frame_stats(double frame_time_ms, int success) {
    pthread_mutex_lock(&stats_mutex);
    
    if (success) {
        stats.frames_sent++;
        
        // Update timing statistics with exponential moving average
        if (stats.frames_sent == 1) {
            stats.avg_frame_time_ms = frame_time_ms;
        } else {
            stats.avg_frame_time_ms = 0.9 * stats.avg_frame_time_ms + 0.1 * frame_time_ms;
        }
        
        if (frame_time_ms > stats.max_frame_time_ms) {
            stats.max_frame_time_ms = frame_time_ms;
        }
        
        // Calculate FPS (simple moving average over last few frames)
        if (frame_time_ms > 0) {
            stats.current_fps = (uint32_t)(1000.0 / stats.avg_frame_time_ms);
        }
    } else {
        stats.frames_dropped++;
    }
    
    stats.last_communication = time(NULL);
    
    pthread_mutex_unlock(&stats_mutex);
}

/****
 *
 * Updates the connection status of left and right LED matrix devices
 *
 * DESCRIPTION:
 *   Tracks the connection state of both LED matrix devices and detects
 *   reconnection events. Increments reconnection counter when a device
 *   transitions from disconnected to connected state. Logs reconnection
 *   events for debugging and monitoring purposes.
 *
 * PARAMETERS:
 *   left_connected - Non-zero if left device is connected, zero if disconnected
 *   right_connected - Non-zero if right device is connected, zero if disconnected
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Updates device connection status in global statistics
 *   - Increments reconnection counter for state transitions
 *   - Logs reconnection events to system log
 *   - Stores previous connection states for comparison
 *
 * SECURITY FEATURES:
 *   - Thread-safe updates using mutex protection
 *   - State transition detection prevents false reconnection counts
 *
 * MEMORY MANAGEMENT:
 *   Updates global static storage only. No dynamic allocation.
 *
 ****/
void update_device_status(int left_connected, int right_connected) {
    pthread_mutex_lock(&stats_mutex);
    
    int prev_left = stats.left_device_connected;
    int prev_right = stats.right_device_connected;
    
    stats.left_device_connected = left_connected;
    stats.right_device_connected = right_connected;
    
    // Count reconnections
    if (!prev_left && left_connected) {
        stats.device_reconnects++;
        LOG_INFO("Left device reconnected");
    }
    if (!prev_right && right_connected) {
        stats.device_reconnects++;
        LOG_INFO("Right device reconnected");
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

/****
 *
 * Increments error counters based on error type classification
 *
 * DESCRIPTION:
 *   Updates specific error counters based on the error type string provided.
 *   Supports tracking of communication errors, configuration reloads, and
 *   thread restart events. Uses string comparison to categorize errors into
 *   appropriate statistical buckets for monitoring and debugging.
 *
 * PARAMETERS:
 *   error_type - String identifying the type of error ("communication", "config_reload", "thread_restart")
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Increments appropriate error counter in global statistics
 *   - Acquires and releases statistics mutex
 *
 * SECURITY FEATURES:
 *   - Thread-safe updates using mutex protection
 *   - String comparison uses safe strcmp function
 *   - Handles unknown error types gracefully (no action taken)
 *
 * MEMORY MANAGEMENT:
 *   Uses only stack variables for string comparison. No dynamic allocation.
 *
 ****/
void update_error_count(const char* error_type) {
    pthread_mutex_lock(&stats_mutex);
    
    if (strcmp(error_type, "communication") == 0) {
        stats.communication_errors++;
    } else if (strcmp(error_type, "config_reload") == 0) {
        stats.config_reloads++;
    } else if (strcmp(error_type, "thread_restart") == 0) {
        stats.thread_restarts++;
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

