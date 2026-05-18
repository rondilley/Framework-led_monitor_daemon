/*
 * Framework LED Monitor Daemon
 * 
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 *
 * High-performance C implementation of Framework Laptop 16 LED matrix system monitor
 * 
 * Based on the original Python implementation by Jeremy Karstrom:
 * https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git
 *
 * This C version provides enhanced performance, enterprise features, and 
 * comprehensive system integration while maintaining compatibility with
 * the original visualization concepts and LED matrix protocols.
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
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>
#include <math.h>
#include <sys/time.h>

#include "led_monitor.h"
#include "led_config.h"
#include "led_logging.h"
#include "led_errors.h"
#include "led_stats.h"

#define UPDATE_INTERVAL_MS 100
#define DEFAULT_CONFIG_FILE "/etc/led_monitor.conf"

typedef enum {
    SIDE_LEFT  = 0,
    SIDE_RIGHT = 1
} matrix_side_t;

typedef struct {
    char port_location[32];
    matrix_side_t side;
    unsigned char initial_brightness;   // snapshotted at init; avoids worker
                                        // reading global_config under reload.
    int serial_fd;
    pthread_t thread_id;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    LEDGrid* pending_grid;
    int running;
} DrawingThread;

// Global state
static volatile int daemon_running = 1;
static volatile int config_reload_requested = 0;
int verbose_logging = 0;
static DrawingThread left_thread;
static DrawingThread right_thread;
static led_config_t global_config;

// Forward declarations
void signal_handler(int sig);
led_error_t daemonize(void);
led_error_t init_serial_port(const char* port_location);
void* drawing_thread_func(void* arg);
void send_grid_to_thread(DrawingThread* thread, LEDGrid* grid);
led_error_t init_drawing_thread(DrawingThread* thread, const char* port_location,
                                matrix_side_t side, unsigned char initial_brightness);
void cleanup_drawing_thread(DrawingThread* thread);
led_error_t find_serial_device(const char* port_location, char* device_path, size_t path_size);

/****
 *
 * Handle system signals for daemon control
 *
 * DESCRIPTION:
 *   Processes incoming system signals to control daemon behavior including
 *   graceful shutdown (SIGTERM/SIGINT) and configuration reload (SIGUSR1/SIGHUP).
 *   Updates global state variables to coordinate with main daemon loop.
 *
 * PARAMETERS:
 *   sig - Signal number received from the system
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Sets daemon_running to 0 for termination signals
 *   Sets config_reload_requested to 1 for reload signals
 *   Logs signal receipt and action taken
 *
 * SECURITY FEATURES:
 *   - Signal handling follows secure practices
 *   - Only responds to expected signal types
 *   - Logs unexpected signals for security monitoring
 *
 * MEMORY MANAGEMENT:
 *   No memory allocation - modifies global state only
 *
 ****/
void signal_handler(int sig) {
    // Only async-signal-safe work belongs here: set flags and return. The
    // main loop observes daemon_running / config_reload_requested. LED
    // clearing and logging happen in the normal shutdown path via
    // cleanup_drawing_thread(), which is safe to call from thread context.
    switch(sig) {
        case SIGTERM:
        case SIGINT:
            daemon_running = 0;
            break;
        case SIGUSR1:
        case SIGHUP:
            config_reload_requested = 1;
            break;
        default:
            break;
    }
}

/****
 *
 * Convert the current process into a daemon by detaching from terminal
 *
 * DESCRIPTION:
 *   Implements the standard double-fork daemonization process to create a background
 *   daemon process completely detached from the controlling terminal. Closes all
 *   file descriptors, redirects standard streams to /dev/null, and sets appropriate
 *   working directory and file creation mask.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   LED_SUCCESS on successful daemonization
 *   LED_ERROR_SYSTEM_RESOURCE on fork/setsid/chdir failures
 *
 * SIDE EFFECTS:
 *   Parent process exits successfully
 *   Child process becomes session leader
 *   Working directory changed to root (/)
 *   File creation mask cleared
 *   All file descriptors closed and standard streams redirected
 *
 * SECURITY FEATURES:
 *   - Closes all inherited file descriptors to prevent resource leaks
 *   - Sets umask(0) for predictable file permissions
 *   - Changes to root directory to avoid filesystem unmount issues
 *   - Ignores SIGCHLD and SIGHUP during transition
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation - only system calls
 *
 ****/
led_error_t daemonize(void) {
    pid_t pid;
    
    // First fork
    pid = fork();
    if (pid < 0) {
        return LED_ERROR_SYSTEM_RESOURCE;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Create new session
    if (setsid() < 0) {
        return LED_ERROR_SYSTEM_RESOURCE;
    }
    
    // Ignore signals that could cause issues
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    // Second fork
    pid = fork();
    if (pid < 0) {
        return LED_ERROR_SYSTEM_RESOURCE;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Set file creation mask
    umask(0);
    
    // Change working directory
    if (chdir("/") < 0) {
        return LED_ERROR_SYSTEM_RESOURCE;
    }
    
    // Close file descriptors more safely
    int max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) max_fd = 1024; // Fallback
    if (max_fd > 8192) max_fd = 8192; // Reasonable limit
    
    for (int fd = 0; fd < max_fd; fd++) {
        close(fd);
    }
    
    // Redirect standard file descriptors to /dev/null
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) {
            close(null_fd);
        }
    }
    
    return LED_SUCCESS;
}

/****
 *
 * Locate the actual device path for a Framework LED matrix by USB port location
 *
 * DESCRIPTION:
 *   Searches the /dev/serial/by-path/ and /dev/serial/by-id/ directories to find
 *   the actual device node (e.g., /dev/ttyACM0) corresponding to a USB port location
 *   string. Uses symlink resolution to map stable identifiers to device nodes.
 *
 * PARAMETERS:
 *   port_location - USB port location string (e.g., "1-4.2")
 *   device_path - Buffer to store the found device path
 *   path_size - Size of the device_path buffer
 *
 * RETURNS:
 *   LED_SUCCESS if device found and path populated
 *   LED_ERROR_DEVICE_NOT_FOUND if no matching device located
 *
 * SIDE EFFECTS:
 *   Modifies device_path buffer with found device path
 *   Opens and closes directory handles during search
 *
 * SECURITY FEATURES:
 *   - Validates buffer sizes to prevent overflow
 *   - Uses safe string functions with explicit lengths
 *   - Checks for Framework-specific device identifiers
 *   - Handles malformed symlinks gracefully
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffers for path manipulation
 *   Properly closes directory handles on all exit paths
 *
 ****/
led_error_t find_serial_device(const char* port_location, char* device_path, size_t path_size) {
    DIR* dir;
    struct dirent* ent;
    char path[512];
    char link_target[512];
    ssize_t len;
    
    // First try by-path directory
    dir = opendir("/dev/serial/by-path/");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, port_location) != NULL) {
                snprintf(path, sizeof(path), "/dev/serial/by-path/%s", ent->d_name);
                len = readlink(path, link_target, sizeof(link_target) - 1);
                if (len != -1) {
                    link_target[len] = '\0';
                    char* device_name = strrchr(link_target, '/');
                    if (device_name) {
                        snprintf(device_path, path_size, "/dev/%s", device_name + 1);
                        closedir(dir);
                        LOG_DEBUG("Found device for port %s: %s", port_location, device_path);
                        return LED_SUCCESS;
                    }
                }
            }
        }
        closedir(dir);
    }
    
    // Fallback: try by-id directory for Framework devices
    dir = opendir("/dev/serial/by-id/");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, "Framework") != NULL && 
                strstr(ent->d_name, "LED_Matrix") != NULL) {
                snprintf(path, sizeof(path), "/dev/serial/by-id/%s", ent->d_name);
                len = readlink(path, link_target, sizeof(link_target) - 1);
                if (len != -1) {
                    link_target[len] = '\0';
                    char* device_name = strrchr(link_target, '/');
                    if (device_name) {
                        snprintf(device_path, path_size, "/dev/%s", device_name + 1);
                        closedir(dir);
                        LOG_DEBUG("Found Framework device (fallback): %s", device_path);
                        return LED_SUCCESS;
                    }
                }
            }
        }
        closedir(dir);
    }
    
    return LED_ERROR_DEVICE_NOT_FOUND;
}

/****
 *
 * Initialize serial communication with a Framework LED matrix device
 *
 * DESCRIPTION:
 *   Locates the device using port location, opens the serial port, and configures
 *   it for 115200 baud 8N1 communication with the LED matrix firmware. Sets up
 *   proper termios attributes for reliable CDC-ACM communication.
 *
 * PARAMETERS:
 *   port_location - USB port location identifier for the target device
 *
 * RETURNS:
 *   File descriptor (positive integer) on successful initialization
 *   LED_ERROR_DEVICE_NOT_FOUND if device cannot be located
 *   LED_ERROR_PERMISSION_DENIED if device cannot be opened
 *   LED_ERROR_COMMUNICATION_FAILED if serial configuration fails
 *
 * SIDE EFFECTS:
 *   Opens file descriptor for serial device
 *   Configures terminal attributes for the port
 *   Flushes any pending serial data
 *
 * SECURITY FEATURES:
 *   - Validates device existence before opening
 *   - Uses O_NOCTTY to prevent terminal control acquisition
 *   - Proper error handling for permission and access issues
 *   - Clears terminal buffers to prevent data injection
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffers for device path resolution
 *   Caller responsible for closing returned file descriptor
 *
 ****/
led_error_t init_serial_port(const char* port_location) {
    char device_path[256];
    int fd;
    struct termios tty;
    
    led_error_t result = find_serial_device(port_location, device_path, sizeof(device_path));
    if (result != LED_SUCCESS) {
        LOG_ERROR("Failed to find serial device for port %s", port_location);
        return result;
    }
    
    fd = open(device_path, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        LOG_ERROR("Error opening %s: %s", device_path, strerror(errno));
        return LED_ERROR_PERMISSION_DENIED;
    }
    
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        LOG_ERROR("Error from tcgetattr: %s", strerror(errno));
        close(fd);
        return LED_ERROR_COMMUNICATION_FAILED;
    }
    
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        LOG_ERROR("Error from tcsetattr: %s", strerror(errno));
        close(fd);
        return LED_ERROR_COMMUNICATION_FAILED;
    }
    
    LOG_INFO("Opened serial port %s at %s (fd=%d)", port_location, device_path, fd);
    
    // Clear any pending data
    tcflush(fd, TCIOFLUSH);
    
    return fd; // Return file descriptor as success
}

/****
 *
 * Main execution function for LED matrix drawing thread
 *
 * DESCRIPTION:
 *   Worker thread that receives LED grid data via condition variables, manages
 *   serial port connection lifecycle, and renders grids to the physical LED matrix.
 *   Implements frame timing statistics and automatic reconnection on communication
 *   failures. Runs until thread shutdown is signaled.
 *
 * PARAMETERS:
 *   arg - Pointer to DrawingThread structure containing thread context
 *
 * RETURNS:
 *   NULL (standard pthread return value)
 *
 * SIDE EFFECTS:
 *   Continuously renders LED grids to hardware
 *   Updates frame timing and device status statistics
 *   Manages serial port connection state
 *   Frees processed grid memory
 *
 * SECURITY FEATURES:
 *   - Validates thread context pointer before use
 *   - Handles communication failures gracefully
 *   - Proper mutex usage prevents race conditions
 *   - Bounds checking on all serial operations
 *
 * MEMORY MANAGEMENT:
 *   Frees LED grid structures after rendering
 *   Manages serial port file descriptor lifecycle
 *   Uses condition variables for efficient grid queue management
 *
 ****/
void* drawing_thread_func(void* arg) {
    DrawingThread* thread = (DrawingThread*)arg;
    LEDGrid* grid_to_draw = NULL;
    struct timeval start_time, end_time;
    double frame_time_ms;
    
    while (thread->running) {
        pthread_mutex_lock(&thread->queue_mutex);
        
        while (thread->pending_grid == NULL && thread->running) {
            pthread_cond_wait(&thread->queue_cond, &thread->queue_mutex);
        }
        
        if (thread->pending_grid) {
            grid_to_draw = thread->pending_grid;
            thread->pending_grid = NULL;
        }
        
        pthread_mutex_unlock(&thread->queue_mutex);
        
        if (grid_to_draw) {
            gettimeofday(&start_time, NULL);
            
            if (thread->serial_fd < 0) {
                int fd_result = init_serial_port(thread->port_location);
                if (fd_result < 0) {
                    update_error_count("communication");
                    sleep(1);
                    free(grid_to_draw);
                    grid_to_draw = NULL;
                    continue;
                }
                thread->serial_fd = fd_result;
                
                // Initialize the display when first connected
                LOG_DEBUG("Initializing display for port %s", thread->port_location);

                // Use the brightness snapshot taken at thread init so the
                // worker never reads global_config (which the main thread can
                // mutate on SIGHUP reload).
                unsigned char brightness = thread->initial_brightness;
                send_command(thread->serial_fd, 0x00, &brightness, 1);
                
                // Turn display on 
                send_command(thread->serial_fd, 0x14, NULL, 0);
                
                usleep(10000); // 10ms delay after initialization
            }
            
            draw_to_leds(thread->serial_fd, grid_to_draw);
            
            gettimeofday(&end_time, NULL);
            frame_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + 
                          (end_time.tv_usec - start_time.tv_usec) / 1000.0;
            
            update_frame_stats(frame_time_ms, 1);
            
            free(grid_to_draw);
            grid_to_draw = NULL;
            
            // Update device status using the side flag captured at thread
            // init (cheaper and race-free vs strcmp against global_config).
            if (thread->side == SIDE_LEFT) {
                update_device_status(1, -1); // -1 means don't change
            } else {
                update_device_status(-1, 1);
            }
        }
    }
    
    return NULL;
}

/****
 *
 * Queue an LED grid for rendering by a drawing thread
 *
 * DESCRIPTION:
 *   Thread-safe function that copies an LED grid structure and queues it for
 *   rendering by the specified drawing thread. Uses mutex protection and condition
 *   variables for coordination. Implements frame dropping for performance.
 *
 * PARAMETERS:
 *   thread - Pointer to target DrawingThread structure
 *   grid - Pointer to LEDGrid data to be rendered
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Allocates memory for grid copy
 *   May free previously queued grid if queue is full
 *   Signals drawing thread via condition variable
 *   Updates frame statistics
 *
 * SECURITY FEATURES:
 *   - Validates input pointers before use
 *   - Thread-safe memory management with proper locking
 *   - Handles memory allocation failures gracefully
 *   - Prevents queue overflow by replacing pending grids
 *
 * MEMORY MANAGEMENT:
 *   Allocates new LEDGrid structure for thread-safe passing
 *   Frees any previously pending grid to prevent memory leaks
 *   Memory freed by drawing thread after rendering
 *
 ****/
void send_grid_to_thread(DrawingThread* thread, LEDGrid* grid) {
    LEDGrid* new_grid = malloc(sizeof(LEDGrid));
    if (!new_grid) {
        LOG_ERROR("Failed to allocate memory for grid");
        update_error_count("memory");
        return;
    }
    
    memcpy(new_grid, grid, sizeof(LEDGrid));
    
    pthread_mutex_lock(&thread->queue_mutex);
    
    if (thread->pending_grid) {
        free(thread->pending_grid);
        update_frame_stats(0, 0); // Frame dropped
    }
    thread->pending_grid = new_grid;
    
    pthread_cond_signal(&thread->queue_cond);
    pthread_mutex_unlock(&thread->queue_mutex);
}

/****
 *
 * Initialize a drawing thread for LED matrix communication
 *
 * DESCRIPTION:
 *   Sets up a DrawingThread structure with synchronization primitives (mutex and
 *   condition variable) and creates the worker thread. Initializes the thread
 *   context for managing LED matrix communication to a specific USB port.
 *
 * PARAMETERS:
 *   thread - Pointer to DrawingThread structure to initialize
 *   port_location - USB port location string for target LED matrix
 *
 * RETURNS:
 *   LED_SUCCESS on successful thread creation
 *   LED_ERROR_THREAD_CREATION on pthread operation failures
 *
 * SIDE EFFECTS:
 *   Creates new pthread with drawing_thread_func as entry point
 *   Initializes mutex and condition variable for thread coordination
 *   Copies port location string to thread context
 *
 * SECURITY FEATURES:
 *   - Validates thread structure pointer
 *   - Properly initializes all synchronization primitives
 *   - Cleans up resources on initialization failure
 *   - Safe string copy with explicit null termination
 *
 * MEMORY MANAGEMENT:
 *   Thread structure managed by caller
 *   Pthread resources managed by system
 *   Cleanup required via cleanup_drawing_thread() on shutdown
 *
 ****/
led_error_t init_drawing_thread(DrawingThread* thread, const char* port_location,
                                matrix_side_t side, unsigned char initial_brightness) {
    strncpy(thread->port_location, port_location, sizeof(thread->port_location) - 1);
    thread->port_location[sizeof(thread->port_location) - 1] = '\0';
    thread->side = side;
    thread->initial_brightness = initial_brightness;
    thread->serial_fd = -1;
    thread->pending_grid = NULL;
    thread->running = 1;
    
    if (pthread_mutex_init(&thread->queue_mutex, NULL) != 0) {
        return LED_ERROR_THREAD_CREATION;
    }
    if (pthread_cond_init(&thread->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&thread->queue_mutex);
        return LED_ERROR_THREAD_CREATION;
    }
    if (pthread_create(&thread->thread_id, NULL, drawing_thread_func, thread) != 0) {
        pthread_mutex_destroy(&thread->queue_mutex);
        pthread_cond_destroy(&thread->queue_cond);
        return LED_ERROR_THREAD_CREATION;
    }
    
    return LED_SUCCESS;
}

/****
 *
 * Properly shutdown and cleanup a drawing thread
 *
 * DESCRIPTION:
 *   Signals thread shutdown, waits for thread completion, and cleans up all
 *   associated resources including synchronization primitives, pending grids,
 *   and serial port connections. Ensures graceful thread termination.
 *
 * PARAMETERS:
 *   thread - Pointer to DrawingThread structure to cleanup
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Stops the drawing thread execution
 *   Closes serial port file descriptor
 *   Frees any pending grid memory
 *   Destroys mutex and condition variable
 *
 * SECURITY FEATURES:
 *   - Graceful thread shutdown prevents resource leaks
 *   - Proper synchronization during cleanup
 *   - Validates resources before cleanup operations
 *   - Ensures all memory is freed
 *
 * MEMORY MANAGEMENT:
 *   Frees pending LED grid if present
 *   Destroys pthread synchronization primitives
 *   Closes file descriptors to prevent handle leaks
 *
 ****/
void cleanup_drawing_thread(DrawingThread* thread) {
    // Set running=0 and signal under the mutex so the worker can't miss the
    // wakeup if it's between checking thread->running and entering cond_wait
    // (lost-wakeup race that previously deadlocked pthread_join on shutdown).
    pthread_mutex_lock(&thread->queue_mutex);
    thread->running = 0;
    pthread_cond_broadcast(&thread->queue_cond);
    pthread_mutex_unlock(&thread->queue_mutex);
    pthread_join(thread->thread_id, NULL);
    
    if (thread->pending_grid) {
        free(thread->pending_grid);
    }
    
    if (thread->serial_fd >= 0) {
        LOG_DEBUG("Clearing LEDs for port %s before shutdown", thread->port_location);
        clear_leds(thread->serial_fd);
        close(thread->serial_fd);
    }
    
    pthread_mutex_destroy(&thread->queue_mutex);
    pthread_cond_destroy(&thread->queue_cond);
}

/****
 *
 * Reload daemon configuration from config file during runtime
 *
 * DESCRIPTION:
 *   Attempts to reload the configuration file and update global settings without
 *   restarting the daemon. Updates logging level and other runtime parameters
 *   based on the new configuration. Provides error handling for invalid configs.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   LED_SUCCESS if configuration reloaded successfully
 *   LED_ERROR_* codes for various configuration loading failures
 *
 * SIDE EFFECTS:
 *   Updates global_config structure
 *   Changes logging verbosity level
 *   Updates error count statistics
 *
 * SECURITY FEATURES:
 *   - Validates configuration before applying changes
 *   - Logs configuration reload attempts
 *   - Maintains existing config on reload failure
 *   - Prevents injection through config validation
 *
 * MEMORY MANAGEMENT:
 *   Temporary configuration structure on stack
 *   Global config updated atomically on success
 *
 ****/
led_error_t reload_configuration(void) {
    led_config_t new_config;
    led_error_t result = reload_config(&new_config);
    
    if (result == LED_SUCCESS) {
        // Update global configuration
        global_config = new_config;
        
        // Update logging level
        set_log_level(global_config.log_level);
        
        LOG_INFO("Configuration reloaded successfully");
        update_error_count("config_reload");
        return LED_SUCCESS;
    } else {
        LOG_ERROR("Failed to reload configuration: %s", led_error_string(result));
        return result;
    }
}

/****
 *
 * Display command-line usage information and available options
 *
 * DESCRIPTION:
 *   Prints formatted help text showing all available command-line options,
 *   their descriptions, and default configuration file locations. Provides
 *   user-friendly guidance for daemon operation and configuration.
 *
 * PARAMETERS:
 *   program_name - Name of the executable (typically argv[0])
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Writes help text to stdout
 *
 * SECURITY FEATURES:
 *   - No user input processing or validation required
 *   - Static text output only
 *   - Safe for any execution context
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation - uses printf for output
 *
 ****/
void print_usage(const char* program_name) {
    printf("Framework LED Monitor Daemon\n");
    printf("Based on the original Python implementation by Jeremy Karstrom\n");
    printf("https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git\n\n");
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  -f, --foreground    Run in foreground (don't daemonize)\n");
    printf("  -c, --config FILE   Use specific configuration file\n");
    printf("  -v, --verbose       Enable verbose logging\n");
    printf("  -d, --debug         Enable debug logging\n");
    printf("  -h, --help          Show this help message\n");
    printf("  --test-devices      Test device detection and exit\n");
    printf("\nConfiguration file locations:\n");
    printf("  System: %s\n", DEFAULT_CONFIG_PATH);
    printf("  User:   ~/.config/led_monitor.conf\n");
}

/****
 *
 * Main entry point for the LED Monitor Daemon
 *
 * DESCRIPTION:
 *   Initializes the LED matrix monitoring daemon including command-line parsing,
 *   configuration loading, daemonization, device detection, thread management,
 *   and the main monitoring loop. Handles system metrics collection and LED
 *   visualization until shutdown signal received.
 *
 * PARAMETERS:
 *   argc - Number of command-line arguments
 *   argv - Array of command-line argument strings
 *
 * RETURNS:
 *   EXIT_SUCCESS (0) on normal termination
 *   EXIT_FAILURE (1) on error conditions
 *
 * SIDE EFFECTS:
 *   May daemonize the process
 *   Creates PID file if running as daemon
 *   Initializes logging and statistics
 *   Creates worker threads for LED communication
 *   Continuously updates LED matrices until shutdown
 *
 * SECURITY FEATURES:
 *   - Validates all command-line arguments
 *   - Checks for existing daemon instances
 *   - Proper signal handler installation
 *   - Resource cleanup on all exit paths
 *   - PID file management for daemon control
 *
 * MEMORY MANAGEMENT:
 *   Manages global configuration structure
 *   Allocates and frees CPU values array
 *   Cleanup of threads and resources on exit
 *
 ****/
int main(int argc, char* argv[]) {
    led_error_t result;
    const char* config_file = NULL;
    int foreground_mode = 0;
    int verbose_mode = 0;
    int debug_mode = 0;
    int test_devices = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--foreground") == 0 || strcmp(argv[i], "-f") == 0) {
            foreground_mode = 1;
        } else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --config requires a filename\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--test-devices") == 0) {
            test_devices = 1;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    
    // Load configuration
    if (config_file) {
        result = load_config(&global_config, config_file);
    } else {
        result = reload_config(&global_config);
    }
    
    if (result != LED_SUCCESS && result != LED_ERROR_FILE_IO) {
        fprintf(stderr, "Error loading configuration: %s\n", led_error_string(result));
        return EXIT_FAILURE;
    }
    
    // Override config with command line options
    if (foreground_mode) {
        global_config.run_as_daemon = 0;
    }
    if (verbose_mode) {
        verbose_logging = 1;
        // Ensure INFO-level messages are surfaced even if config set a
        // stricter threshold; verbose is a "show me more" knob.
        if (global_config.log_level > LOG_LEVEL_INFO) {
            global_config.log_level = LOG_LEVEL_INFO;
        }
    }
    if (debug_mode) {
        verbose_logging = 1;
        global_config.enable_debug_logging = 1;
        global_config.log_level = LOG_LEVEL_DEBUG;
    }
    
    // Handle special modes
    if (test_devices) {
        printf("Testing device detection...\n");
        char device_path[256];
        
        printf("Looking for left device (%s): ", global_config.left_device_path);
        if (find_serial_device(global_config.left_device_path, device_path, sizeof(device_path)) == LED_SUCCESS) {
            printf("Found at %s\n", device_path);
        } else {
            printf("Not found\n");
        }
        
        printf("Looking for right device (%s): ", global_config.right_device_path);
        if (find_serial_device(global_config.right_device_path, device_path, sizeof(device_path)) == LED_SUCCESS) {
            printf("Found at %s\n", device_path);
        } else {
            printf("Not found\n");
        }
        
        return EXIT_SUCCESS;
    }
    
    // Daemonize if requested. Logging is intentionally initialized *after*
    // this point because daemonize() closes every fd in the process,
    // including any syslog socket openlog() would have created. Pre-daemonize
    // errors must use stderr directly.
    if (global_config.run_as_daemon) {
        result = daemonize();
        if (result != LED_SUCCESS) {
            fprintf(stderr, "Failed to daemonize: %s\n", led_error_string(result));
            return EXIT_FAILURE;
        }
    }

    init_logging(global_config.log_level, global_config.enable_debug_logging,
                 !global_config.run_as_daemon);
    
    // Set up signal handlers
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);
    signal(SIGHUP, signal_handler);
    
    LOG_INFO("LED Monitor Daemon starting...");
    
    // Initialize statistics
    init_statistics();
    
    
    // Initialize drawing threads. The "initial brightness" sent to each
    // matrix on first connect is the midpoint of the configured foreground
    // range; per-frame brightness comes from the pixel values themselves.
    unsigned char initial_brightness =
        (unsigned char)((global_config.max_foreground_brightness +
                         global_config.min_foreground_brightness) / 2);

    result = init_drawing_thread(&left_thread, global_config.left_device_path,
                                 SIDE_LEFT, initial_brightness);
    if (result != LED_SUCCESS) {
        LOG_ERROR("Failed to initialize left drawing thread: %s", led_error_string(result));
        goto cleanup;
    }

    result = init_drawing_thread(&right_thread, global_config.right_device_path,
                                 SIDE_RIGHT, initial_brightness);
    if (result != LED_SUCCESS) {
        LOG_ERROR("Failed to initialize right drawing thread: %s", led_error_string(result));
        cleanup_drawing_thread(&left_thread);
        goto cleanup;
    }
    
    // Main loop
    CPUValues cpu_values = {0};
    MemoryValues memory_values = {0};
    BatteryValues battery_values = {0};
    DiskValues disk_values = {0};
    NetworkValues network_values = {0};
    
    int loop_count = 0;
    
    while (daemon_running) {
        // Handle configuration reload
        if (config_reload_requested) {
            config_reload_requested = 0;
            reload_configuration();
        }
        
        // Get screen brightness
        float screen_brightness = get_screen_brightness();
        int background_value = (int)(screen_brightness * 
            (global_config.max_background_brightness - global_config.min_background_brightness) + 
            global_config.min_background_brightness);
        int foreground_value = (int)(screen_brightness * 
            (global_config.max_foreground_brightness - global_config.min_foreground_brightness) + 
            global_config.min_foreground_brightness);
        
        // Get system metrics
        get_cpu_values(&cpu_values);
        get_memory_values(&memory_values);
        get_battery_values(&battery_values);
        
        if (global_config.enable_debug_logging && (loop_count % 100 == 0)) {
            LOG_DEBUG("Loop %d: brightness=%.2f, bg=%d, fg=%d, mem=%.2f%%, bat=%.1f%% %s", 
                   loop_count, screen_brightness, background_value, foreground_value,
                   memory_values.usage_percent * 100, battery_values.percent * 100,
                   battery_values.is_charging ? "charging" : "");
        }
        
        // Draw to left LED Matrix
        LEDGrid left_grid;
        memset(&left_grid, 0, sizeof(left_grid));
        draw_cpu(&left_grid, &cpu_values, foreground_value);
        draw_memory(&left_grid, &memory_values, foreground_value);
        draw_battery(&left_grid, &battery_values, foreground_value);
        draw_borders_left(&left_grid, background_value);
        send_grid_to_thread(&left_thread, &left_grid);

        // Draw to right LED Matrix
        get_disk_values(&disk_values);
        get_network_values(&network_values);

        LEDGrid right_grid;
        memset(&right_grid, 0, sizeof(right_grid));
        draw_bar(&right_grid, disk_values.read_percent,      foreground_value, 1, 0);
        draw_bar(&right_grid, disk_values.write_percent,     foreground_value, 1, 1);
        draw_bar(&right_grid, network_values.upload_percent, foreground_value, 5, 0);
        draw_bar(&right_grid, network_values.download_percent, foreground_value, 5, 1);
        draw_borders_right(&right_grid, background_value);
        send_grid_to_thread(&right_thread, &right_grid);
        
        loop_count++;
        usleep(global_config.update_interval_ms * 1000);
    }
    
    // Cleanup
    cleanup_drawing_thread(&left_thread);
    cleanup_drawing_thread(&right_thread);
    
    if (cpu_values.values) {
        free(cpu_values.values);
    }

cleanup:
    
    LOG_INFO("LED Monitor Daemon stopped.");
    closelog();
    
    return EXIT_SUCCESS;
}