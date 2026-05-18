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

#include "led_errors.h"

/****
 *
 * Converts LED error enumeration to human-readable error message string
 *
 * DESCRIPTION:
 *   Provides descriptive error messages for LED system error codes.
 *   This function maps internal error enumeration values to user-friendly
 *   error descriptions that can be displayed to users or logged for
 *   debugging purposes. Essential for error reporting and troubleshooting.
 *
 * PARAMETERS:
 *   error - LED error enumeration value to convert
 *
 * RETURNS:
 *   Constant string pointer containing error description, or "Unknown error" for invalid codes
 *
 * SIDE EFFECTS:
 *   None
 *
 * SECURITY FEATURES:
 *   - Returns constant string literals only
 *   - Default case handles invalid error codes safely
 *   - No buffer operations or dynamic content
 *   - Input validation through exhaustive switch statement
 *
 * MEMORY MANAGEMENT:
 *   Returns pointers to static string literals - no dynamic allocation required
 *
 ****/
const char* led_error_string(led_error_t error) {
    switch (error) {
        case LED_SUCCESS:
            return "Success";
        case LED_ERROR_DEVICE_NOT_FOUND:
            return "LED device not found";
        case LED_ERROR_PERMISSION_DENIED:
            return "Permission denied accessing device";
        case LED_ERROR_COMMUNICATION_FAILED:
            return "Communication with device failed";
        case LED_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case LED_ERROR_INVALID_PARAMETER:
            return "Invalid parameter provided";
        case LED_ERROR_CONFIG_PARSE:
            return "Configuration file parse error";
        case LED_ERROR_SYSTEM_RESOURCE:
            return "System resource unavailable";
        case LED_ERROR_THREAD_CREATION:
            return "Thread creation failed";
        case LED_ERROR_FILE_IO:
            return "File I/O error";
        default:
            return "Unknown error";
    }
}