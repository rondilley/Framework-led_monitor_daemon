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

#ifndef LED_ERRORS_H
#define LED_ERRORS_H

typedef enum {
    LED_SUCCESS = 0,
    LED_ERROR_DEVICE_NOT_FOUND = -1,
    LED_ERROR_PERMISSION_DENIED = -2,
    LED_ERROR_COMMUNICATION_FAILED = -3,
    LED_ERROR_MEMORY_ALLOCATION = -4,
    LED_ERROR_INVALID_PARAMETER = -5,
    LED_ERROR_CONFIG_PARSE = -6,
    LED_ERROR_SYSTEM_RESOURCE = -7,
    LED_ERROR_THREAD_CREATION = -8,
    LED_ERROR_FILE_IO = -9
} led_error_t;

const char* led_error_string(led_error_t error);

#endif // LED_ERRORS_H