/*
 * Framework LED Monitor Daemon - Header Definitions
 * 
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 * 
 * Based on the original Python implementation by Jeremy Karstrom:
 * https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git
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

#ifndef LED_MONITOR_H
#define LED_MONITOR_H

#define GRID_WIDTH 34
#define GRID_HEIGHT 9

typedef struct {
    int grid[GRID_HEIGHT][GRID_WIDTH];
} LEDGrid;

typedef struct {
    float* values;
    int count;
} CPUValues;

typedef struct {
    float usage_percent;
} MemoryValues;

typedef struct {
    float percent;
    int is_charging;
} BatteryValues;

typedef struct {
    float read_percent;
    float write_percent;
} DiskValues;

typedef struct {
    float upload_percent;
    float download_percent;
} NetworkValues;

// System monitoring functions
void get_cpu_values(CPUValues* cpu);
void get_memory_values(MemoryValues* mem);
void get_battery_values(BatteryValues* bat);
void get_disk_values(DiskValues* disk);
void get_network_values(NetworkValues* net);
float get_screen_brightness(void);

// LED drawing functions
void draw_cpu(LEDGrid* grid, CPUValues* cpu, int fill_value);
void draw_memory(LEDGrid* grid, MemoryValues* mem, int fill_value);
void draw_battery(LEDGrid* grid, BatteryValues* bat, int fill_value);
void draw_borders_left(LEDGrid* grid, int value);
void draw_bar(LEDGrid* grid, float percent, int fill_value, int x_offset, int at_bottom);
void draw_borders_right(LEDGrid* grid, int value);

// Serial communication functions
void send_command(int fd, unsigned char command, unsigned char* params, int param_len);
void draw_to_leds(int fd, LEDGrid* grid);
void clear_leds(int fd);

#endif // LED_MONITOR_H