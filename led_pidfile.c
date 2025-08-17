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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <signal.h>
#include "led_pidfile.h"

/****
 *
 * Creates a PID file with exclusive locking to prevent multiple daemon instances
 *
 * DESCRIPTION:
 *   Creates a PID file at the specified path with exclusive access control. Uses
 *   file locking mechanisms to prevent multiple instances of the daemon from running
 *   simultaneously. If a PID file already exists, checks if the process is still
 *   running and removes stale files automatically.
 *
 * PARAMETERS:
 *   pid_file_path - Path to the PID file to create (must be writable location)
 *
 * RETURNS:
 *   LED_SUCCESS - PID file created successfully
 *   LED_ERROR_DAEMON_ALREADY_RUNNING - Another instance is already running
 *   LED_ERROR_FILE_IO - File operation failed (permissions, disk space, etc.)
 *
 * SIDE EFFECTS:
 *   - Creates a file on the filesystem
 *   - Applies an exclusive advisory lock on the file descriptor
 *   - May remove stale PID files from previous crashed instances
 *   - Keeps the file descriptor open to maintain the lock
 *
 * SECURITY FEATURES:
 *   - Uses O_EXCL flag for atomic file creation
 *   - Advisory file locking prevents race conditions
 *   - File permissions set to 0644 (owner read/write, group/other read)
 *   - Validates existing processes using kill(pid, 0) signal test
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated buffer for PID string formatting. File descriptor
 *   remains open for the lifetime of the process to maintain the lock.
 *
 ****/
led_error_t create_pid_file(const char* pid_file_path) {
    int fd;
    char pid_str[16];
    
    // Open PID file with exclusive lock
    fd = open(pid_file_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            // Check if existing process is still running
            led_error_t result = check_pid_file(pid_file_path);
            if (result == LED_SUCCESS) {
                return LED_ERROR_DAEMON_ALREADY_RUNNING;
            }
            // Stale PID file, remove and try again
            unlink(pid_file_path);
            fd = open(pid_file_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
            if (fd < 0) {
                return LED_ERROR_FILE_IO;
            }
        } else {
            return LED_ERROR_FILE_IO;
        }
    }
    
    // Apply advisory lock
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        unlink(pid_file_path);
        return LED_ERROR_DAEMON_ALREADY_RUNNING;
    }
    
    // Write PID to file
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (write(fd, pid_str, strlen(pid_str)) < 0) {
        close(fd);
        unlink(pid_file_path);
        return LED_ERROR_FILE_IO;
    }
    
    // Keep file open to maintain lock
    // File will be automatically closed and unlocked on process exit
    return LED_SUCCESS;
}

/****
 *
 * Removes the PID file from the filesystem
 *
 * DESCRIPTION:
 *   Safely removes the PID file at the specified path. Handles cases where
 *   the file may not exist (already removed) without reporting an error.
 *   This function should be called during daemon shutdown to clean up.
 *
 * PARAMETERS:
 *   pid_file_path - Path to the PID file to remove
 *
 * RETURNS:
 *   LED_SUCCESS - File removed successfully or did not exist
 *   LED_ERROR_FILE_IO - File removal failed due to permissions or other I/O error
 *
 * SIDE EFFECTS:
 *   - Removes a file from the filesystem
 *   - Any file locks are automatically released when file is unlinked
 *
 * SECURITY FEATURES:
 *   - Gracefully handles ENOENT (file not found) without error
 *   - Only attempts to remove the specific file path provided
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation. Uses only system calls.
 *
 ****/
led_error_t remove_pid_file(const char* pid_file_path) {
    if (unlink(pid_file_path) < 0) {
        if (errno != ENOENT) {
            return LED_ERROR_FILE_IO;
        }
    }
    return LED_SUCCESS;
}

/****
 *
 * Checks if a PID file exists and the referenced process is still running
 *
 * DESCRIPTION:
 *   Reads a PID file and verifies if the process ID contained within refers
 *   to a currently running process. Uses the kill(pid, 0) system call to
 *   test process existence without sending an actual signal. This function
 *   is used to detect stale PID files from crashed daemon instances.
 *
 * PARAMETERS:
 *   pid_file_path - Path to the PID file to check
 *
 * RETURNS:
 *   LED_SUCCESS - PID file exists and process is running
 *   LED_ERROR_DEVICE_NOT_FOUND - PID file exists but process is not running
 *   LED_ERROR_FILE_IO - Could not read PID file or invalid PID format
 *
 * SIDE EFFECTS:
 *   - Opens and reads from the filesystem
 *   - May send a signal 0 to another process for existence check
 *
 * SECURITY FEATURES:
 *   - Validates PID format using fscanf with format specifier
 *   - Uses kill(pid, 0) which only tests existence, sends no harmful signal
 *   - Properly closes file handles to prevent resource leaks
 *
 * MEMORY MANAGEMENT:
 *   Uses stack variables only. File handle is properly closed before return.
 *
 ****/
led_error_t check_pid_file(const char* pid_file_path) {
    FILE* fp;
    int pid;
    
    fp = fopen(pid_file_path, "r");
    if (!fp) {
        return LED_ERROR_FILE_IO;
    }
    
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        return LED_ERROR_FILE_IO;
    }
    
    fclose(fp);
    
    // Check if process is still running
    if (kill(pid, 0) == 0) {
        return LED_SUCCESS; // Process is running
    } else {
        return LED_ERROR_DEVICE_NOT_FOUND; // Process not found
    }
}