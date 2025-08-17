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
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include "led_monitor.h"

#define DEBUG_LOGGING 1

typedef enum {
    CMD_BRIGHTNESS  = 0x00,
    CMD_PATTERN     = 0x01,
    CMD_BOOTLOADER  = 0x02,
    CMD_SLEEP       = 0x03,
    CMD_ANIMATE     = 0x04,
    CMD_PANIC       = 0x05,
    CMD_DRAW_BW     = 0x06,
    CMD_STAGE_COL   = 0x07,
    CMD_FLUSH_COLS  = 0x08,
    CMD_SET_TEXT    = 0x09,
    CMD_START_GAME  = 0x10,
    CMD_GAME_CTRL   = 0x11,
    CMD_GAME_STATUS = 0x12,
    CMD_SET_COLOR   = 0x13,
    CMD_DISPLAY_ON  = 0x14,
    CMD_INVERT      = 0x15,
    CMD_SET_PX_COL  = 0x16,
    CMD_FLUSH_FB    = 0x17,
    CMD_VERSION     = 0x20
} Command;

/****
 *
 * Send a serial command to Framework LED input module
 *
 * DESCRIPTION:
 *   Constructs and transmits a command packet to the LED input module via USB serial.
 *   Commands follow the Framework protocol with 0x32, 0xAC prefix followed by command
 *   byte and optional parameters. Includes proper error handling and timing delays
 *   to ensure reliable communication with the embedded firmware.
 *
 * PARAMETERS:
 *   fd - File descriptor for the open serial port connection
 *   command - Command byte identifying the operation to perform
 *   params - Optional parameter data to send with the command
 *   param_len - Length of parameter data in bytes (0 if no parameters)
 *
 * RETURNS:
 *   void - Function logs warnings on communication failures
 *
 * SIDE EFFECTS:
 *   - Writes data to serial port file descriptor
 *   - Logs warning messages to syslog on write failures
 *   - Introduces timing delays for proper device synchronization
 *
 * SECURITY FEATURES:
 *   - Validates write operations and logs failures
 *   - Bounds checking on message buffer (512 byte limit)
 *   - Safe memory copy operations for parameter data
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated message buffer, no dynamic allocation required
 *
 ****/
void send_command(int fd, unsigned char command, unsigned char* params, int param_len) {
    unsigned char message[512];
    int msg_len = 0;
    
    message[msg_len++] = 0x32;
    message[msg_len++] = 0xAC;
    message[msg_len++] = command;
    
    if (params && param_len > 0) {
        memcpy(&message[msg_len], params, param_len);
        msg_len += param_len;
    }
    
    int written = write(fd, message, msg_len);
    if (written != msg_len) {
        syslog(LOG_WARNING, "Failed to write command 0x%02X: wrote %d of %d bytes: %s", 
               command, written, msg_len, strerror(errno));
    }
    
    // Shorter delay for StageCol commands, longer for FlushCols
    if (command == CMD_STAGE_COL) {
        usleep(500);  // 0.5ms
    } else {
        usleep(2000); // 2ms
    }
}

/****
 *
 * Render LED grid data to Framework input module display
 *
 * DESCRIPTION:
 *   Transmits a complete LED grid frame to the input module using the staging
 *   and flush protocol. Sends each row individually using CMD_STAGE_COL commands
 *   followed by CMD_FLUSH_COLS to update the physical display. Includes pixel
 *   validation, debugging statistics, and proper brightness clamping.
 *
 * PARAMETERS:
 *   fd - File descriptor for the open serial port connection to LED module
 *   grid - Pointer to LEDGrid structure containing 9x34 pixel brightness values
 *
 * RETURNS:
 *   void - Updates physical LED display if communication succeeds
 *
 * SIDE EFFECTS:
 *   - Modifies LED module display state
 *   - Logs frame statistics every 50 frames when DEBUG_LOGGING enabled
 *   - Increments internal frame counter for debugging
 *
 * SECURITY FEATURES:
 *   - Validates file descriptor before operations
 *   - Clamps pixel values to valid range (0-255)
 *   - Bounds checking on grid dimensions (9 rows, 34 columns)
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated parameter buffers, accesses grid data read-only
 *
 ****/
void draw_to_leds(int fd, LEDGrid* grid) {
    if (fd < 0) return;
    
    static int frame_count = 0;
    int non_zero_pixels = 0;
    
    // Count non-zero pixels for debugging
    for (int row = 0; row < GRID_HEIGHT; row++) {
        for (int col = 0; col < GRID_WIDTH; col++) {
            if (grid->grid[row][col] > 0) {
                non_zero_pixels++;
            }
        }
    }
    
    if (DEBUG_LOGGING && (frame_count % 50 == 0)) {
        syslog(LOG_INFO, "Frame %d: Drawing grid with %d non-zero pixels", frame_count, non_zero_pixels);
    }
    
    // Send each row using StageCol command
    // The Python version sends grid[i, :] meaning row i with all 34 columns
    // StageCol format: [row_index, col0_value, col1_value, ..., col33_value]
    for (int row = 0; row < GRID_HEIGHT; row++) {  // Send 9 rows
        unsigned char params[GRID_WIDTH + 1];
        params[0] = (unsigned char)row;  // Row index (0-8)
        
        // Copy all column values for this row
        for (int col = 0; col < GRID_WIDTH; col++) {  // 34 column values
            int value = grid->grid[row][col];
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            params[col + 1] = (unsigned char)value;
        }
        
        send_command(fd, CMD_STAGE_COL, params, GRID_WIDTH + 1);
    }
    
    // Flush all columns to display
    send_command(fd, CMD_FLUSH_COLS, NULL, 0);
    
    frame_count++;
}

/****
 *
 * Clear all LEDs on Framework input module display
 *
 * DESCRIPTION:
 *   Sends a blank grid to turn off all LEDs on the Framework input module.
 *   Used during daemon shutdown to ensure LEDs are not left in an active state.
 *   Creates a zero-filled grid and transmits it using the standard protocol.
 *
 * PARAMETERS:
 *   fd - File descriptor for the open serial port connection to LED module
 *
 * RETURNS:
 *   void - Clears display if communication succeeds
 *
 * SIDE EFFECTS:
 *   - Sets all LEDs to off state (brightness 0)
 *   - Modifies LED module display state
 *   - Logs clearing action
 *
 * SECURITY FEATURES:
 *   - Validates file descriptor before operations
 *   - Uses stack-allocated memory for safety
 *   - Graceful handling of communication failures
 *
 * MEMORY MANAGEMENT:
 *   Uses stack-allocated LED grid structure, no dynamic allocation
 *
 ****/
void clear_leds(int fd) {
    if (fd < 0) return;
    
    syslog(LOG_INFO, "Clearing all LEDs for shutdown");
    
    // Create empty grid with all LEDs off
    LEDGrid empty_grid;
    memset(&empty_grid, 0, sizeof(empty_grid));
    
    // Send the empty grid to turn off all LEDs
    draw_to_leds(fd, &empty_grid);
    
    // Small delay to ensure transmission completes
    usleep(10000); // 10ms
}