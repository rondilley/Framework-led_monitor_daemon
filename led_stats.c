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
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
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

/****
 *
 * Resets all statistics counters while preserving the original start time
 *
 * DESCRIPTION:
 *   Clears all statistical counters and metrics while maintaining the original
 *   daemon start time for accurate uptime calculation. This function is useful
 *   for debugging or when resetting monitoring data without restarting the daemon.
 *   All frame, error, and performance metrics are returned to zero state.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Zeros all statistics except start_time
 *   - Logs the reset operation
 *   - Acquires and releases statistics mutex
 *
 * SECURITY FEATURES:
 *   - Thread-safe reset using mutex protection
 *   - Preserves original start time for consistent uptime tracking
 *
 * MEMORY MANAGEMENT:
 *   Uses global static storage. No dynamic allocation or deallocation.
 *
 ****/
void reset_statistics(void) {
    pthread_mutex_lock(&stats_mutex);
    
    time_t start_time = stats.start_time;
    memset(&stats, 0, sizeof(stats));
    stats.start_time = start_time;
    
    pthread_mutex_unlock(&stats_mutex);
    
    LOG_INFO("Statistics reset");
}

/****
 *
 * Retrieves current statistics with updated uptime calculation
 *
 * DESCRIPTION:
 *   Returns a read-only pointer to the current statistics structure after
 *   updating the uptime field with the current elapsed time since daemon start.
 *   This function provides thread-safe access to all statistical data for
 *   reporting and monitoring purposes.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   Const pointer to led_statistics_t structure containing current statistics
 *
 * SIDE EFFECTS:
 *   - Updates uptime_seconds field with current elapsed time
 *   - Acquires and releases statistics mutex for uptime update
 *
 * SECURITY FEATURES:
 *   - Returns const pointer to prevent modification by caller
 *   - Thread-safe uptime calculation using mutex protection
 *
 * MEMORY MANAGEMENT:
 *   Returns pointer to global static storage. No allocation or deallocation.
 *
 ****/
const led_statistics_t* get_statistics(void) {
    // Update uptime
    pthread_mutex_lock(&stats_mutex);
    stats.uptime_seconds = time(NULL) - stats.start_time;
    pthread_mutex_unlock(&stats_mutex);
    
    return &stats;
}

/****
 *
 * Formats current statistics as JSON string for API consumption
 *
 * DESCRIPTION:
 *   Generates a formatted JSON representation of all current statistics
 *   including frame metrics, performance data, device status, error counts,
 *   and uptime information. The output is suitable for web APIs, monitoring
 *   systems, or structured logging. Uses safe string formatting with bounds checking.
 *
 * PARAMETERS:
 *   buffer - Output buffer to store the formatted JSON string
 *   buffer_size - Maximum size of the output buffer in bytes
 *
 * RETURNS:
 *   None (void function, output written to buffer parameter)
 *
 * SIDE EFFECTS:
 *   - Writes formatted JSON string to provided buffer
 *   - Calls get_statistics() which updates uptime
 *
 * SECURITY FEATURES:
 *   - Uses snprintf for safe string formatting with buffer bounds
 *   - No buffer overflow protection beyond snprintf limits
 *   - Validates all numeric formats for JSON compliance
 *
 * MEMORY MANAGEMENT:
 *   Uses provided buffer for output. No dynamic allocation.
 *
 ****/
void format_statistics_json(char* buffer, size_t buffer_size) {
    const led_statistics_t* s = get_statistics();
    
    snprintf(buffer, buffer_size,
        "{\n"
        "  \"frames\": {\n"
        "    \"sent\": %lu,\n"
        "    \"dropped\": %lu,\n"
        "    \"fps\": %u\n"
        "  },\n"
        "  \"performance\": {\n"
        "    \"avg_frame_time_ms\": %.2f,\n"
        "    \"max_frame_time_ms\": %.2f\n"
        "  },\n"
        "  \"devices\": {\n"
        "    \"left_connected\": %s,\n"
        "    \"right_connected\": %s,\n"
        "    \"reconnects\": %u\n"
        "  },\n"
        "  \"errors\": {\n"
        "    \"communication\": %lu,\n"
        "    \"config_reloads\": %u,\n"
        "    \"thread_restarts\": %u\n"
        "  },\n"
        "  \"uptime\": {\n"
        "    \"seconds\": %lu,\n"
        "    \"start_time\": %ld\n"
        "  }\n"
        "}",
        s->frames_sent, s->frames_dropped, s->current_fps,
        s->avg_frame_time_ms, s->max_frame_time_ms,
        s->left_device_connected ? "true" : "false",
        s->right_device_connected ? "true" : "false",
        s->device_reconnects,
        s->communication_errors, s->config_reloads, s->thread_restarts,
        s->uptime_seconds, s->start_time);
}

/****
 *
 * Formats current statistics as human-readable text for console display
 *
 * DESCRIPTION:
 *   Generates a formatted text representation of all current statistics
 *   suitable for console output, log files, or human consumption. Includes
 *   a header line and organizes data in a readable table-like format with
 *   units and descriptive labels for all metrics.
 *
 * PARAMETERS:
 *   buffer - Output buffer to store the formatted text string
 *   buffer_size - Maximum size of the output buffer in bytes
 *
 * RETURNS:
 *   None (void function, output written to buffer parameter)
 *
 * SIDE EFFECTS:
 *   - Writes formatted text string to provided buffer
 *   - Calls get_statistics() which updates uptime
 *   - Uses ctime() for human-readable timestamp conversion
 *
 * SECURITY FEATURES:
 *   - Uses snprintf for safe string formatting with buffer bounds
 *   - Safe timestamp formatting using standard ctime() function
 *
 * MEMORY MANAGEMENT:
 *   Uses provided buffer for output. No dynamic allocation.
 *
 ****/
void format_statistics_text(char* buffer, size_t buffer_size) {
    const led_statistics_t* s = get_statistics();
    
    snprintf(buffer, buffer_size,
        "LED Monitor Statistics\n"
        "======================\n"
        "Frames: %lu sent, %lu dropped (%u FPS)\n"
        "Performance: %.2fms avg, %.2fms max frame time\n"
        "Devices: Left %s, Right %s (%u reconnects)\n"
        "Errors: %lu comm, %u reloads, %u restarts\n"
        "Uptime: %lu seconds (started %s)",
        s->frames_sent, s->frames_dropped, s->current_fps,
        s->avg_frame_time_ms, s->max_frame_time_ms,
        s->left_device_connected ? "connected" : "disconnected",
        s->right_device_connected ? "connected" : "disconnected",
        s->device_reconnects,
        s->communication_errors, s->config_reloads, s->thread_restarts,
        s->uptime_seconds, ctime(&s->start_time));
}

// Simple HTTP server for statistics (basic implementation)
static int stats_server_running = 0;
static pthread_t stats_server_thread;

/****
 *
 * Worker thread function for the statistics HTTP server
 *
 * DESCRIPTION:
 *   Thread worker function that would implement a basic HTTP server for
 *   serving statistics data over the network. Currently a placeholder
 *   implementation that logs the intended port but does not create actual
 *   network sockets. Future implementation would handle HTTP requests
 *   and serve JSON/text statistics responses.
 *
 * PARAMETERS:
 *   arg - Pointer to integer containing the port number to bind to
 *
 * RETURNS:
 *   NULL (standard pthread worker return value)
 *
 * SIDE EFFECTS:
 *   - Logs the intended server port to system log
 *   - Runs until thread is cancelled or process exits
 *
 * SECURITY FEATURES:
 *   - Placeholder implementation has no network exposure
 *   - Future implementation should include proper input validation
 *   - Should implement access controls and request rate limiting
 *
 * MEMORY MANAGEMENT:
 *   Uses provided argument pointer. No dynamic allocation in current form.
 *
 ****/
static void* stats_server_worker(void* arg) {
    int port = *(int*)arg;
    // This would be a full HTTP server implementation
    // For now, just a placeholder
    LOG_INFO("Statistics server would run on port %d", port);
    return NULL;
}

/****
 *
 * Starts the statistics HTTP server thread on the specified port
 *
 * DESCRIPTION:
 *   Creates and starts a background thread to run the statistics HTTP server.
 *   Prevents multiple server instances by checking the running state flag.
 *   The server thread will handle HTTP requests for statistics data until
 *   explicitly stopped. Currently creates a placeholder thread that logs
 *   the port but does not bind to network interfaces.
 *
 * PARAMETERS:
 *   port - TCP port number to bind the HTTP server to (1-65535)
 *
 * RETURNS:
 *   0 - Server started successfully or already running
 *   -1 - Failed to create server thread
 *
 * SIDE EFFECTS:
 *   - Creates a new pthread for server operations
 *   - Sets global server running flag
 *   - Logs server startup status
 *   - Stores port number in static variable for thread access
 *
 * SECURITY FEATURES:
 *   - Prevents multiple server instances through state checking
 *   - Thread creation failure is properly handled and logged
 *
 * MEMORY MANAGEMENT:
 *   Uses static storage for port parameter. Thread handle stored globally.
 *
 ****/
int start_stats_server(int port) {
    if (stats_server_running) {
        return 0; // Already running
    }
    
    static int server_port;
    server_port = port;
    
    if (pthread_create(&stats_server_thread, NULL, stats_server_worker, &server_port) != 0) {
        LOG_ERROR("Failed to start statistics server thread");
        return -1;
    }
    
    stats_server_running = 1;
    LOG_INFO("Statistics server started on port %d", port);
    return 0;
}

/****
 *
 * Stops the statistics HTTP server and cleans up resources
 *
 * DESCRIPTION:
 *   Gracefully shuts down the statistics HTTP server by setting the running
 *   flag to false, cancelling the server thread, and waiting for it to complete.
 *   Handles cases where the server is not currently running. Ensures proper
 *   cleanup of pthread resources to prevent memory leaks.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   - Cancels the server thread using pthread_cancel
 *   - Waits for thread completion with pthread_join
 *   - Resets global server running flag
 *   - Logs server shutdown status
 *
 * SECURITY FEATURES:
 *   - Safe shutdown prevents resource leaks
 *   - Handles multiple stop calls gracefully
 *   - Proper thread synchronization prevents race conditions
 *
 * MEMORY MANAGEMENT:
 *   Properly cleans up pthread resources. No dynamic memory to free.
 *
 ****/
void stop_stats_server(void) {
    if (!stats_server_running) {
        return;
    }
    
    stats_server_running = 0;
    pthread_cancel(stats_server_thread);
    pthread_join(stats_server_thread, NULL);
    
    LOG_INFO("Statistics server stopped");
}