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
#include <math.h>
#include "led_monitor.h"

static const int lookup_table[10][3][3] = {
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 0}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 1}, {0, 1, 1}, {0, 0, 0}},
    {{0, 1, 1}, {0, 1, 1}, {0, 0, 1}},
    {{0, 1, 1}, {0, 1, 1}, {0, 1, 1}},
    {{0, 1, 1}, {0, 1, 1}, {1, 1, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 1}},
    {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}}
};

// Original lightning bolt (7 horizontal x 13 vertical) — used by the
// pre-existing layout for systems with <= 8 physical cores.
static const int lightning_bolt_classic[7][13] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,0,0,0,1,1,0},
    {0,0,0,0,1,1,1,0,1,1,1,0,0},
    {0,0,0,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,0,1,1,1,0,0,0,0},
    {0,1,1,0,0,0,1,1,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0}
};

// Compressed lightning bolt (7 horizontal x 5 vertical) — used by the extended
// layout for systems with > 8 cores, where the battery section was reduced
// from 13 to 5 columns to make room for additional CPU cells.
static const int lightning_bolt_compact[7][5] = {
    {0,0,0,0,0},
    {0,0,1,1,0},
    {0,1,1,0,0},
    {1,1,1,1,0},
    {0,0,1,1,0},
    {0,0,1,1,1},
    {0,0,0,0,0}
};

// Selected by draw_cpu() based on physical core count. Other left-matrix
// draw functions consult this to choose section positions/sizes. draw_cpu()
// is the first left-matrix draw call each frame so the flag is current
// before draw_memory/draw_battery/draw_borders_left run.
static int extended_layout = 0;

/****
 *
 * Converts a fill ratio to a lookup table index for LED visualization patterns
 *
 * DESCRIPTION:
 *   Maps a floating-point fill ratio (0.0-1.0) to an integer index (0-9) used
 *   to select visualization patterns from the lookup table. Uses rounding to
 *   ensure smooth transitions between different fill levels.
 *
 * PARAMETERS:
 *   fill_ratio - Float value between 0.0 and 1.0 representing fill percentage
 *
 * RETURNS:
 *   Integer index between 0 and 9 for lookup table access
 *
 * SIDE EFFECTS:
 *   None
 *
 * SECURITY FEATURES:
 *   - Bounds checking ensures returned index is always within valid range [0,9]
 *   - Input clamping prevents array out-of-bounds access
 *
 * MEMORY MANAGEMENT:
 *   No dynamic memory allocation; operates on stack variables only
 *
 ****/
static int spiral_index(float fill_ratio) {
    int index = (int)roundf(fill_ratio * 9.999999f - 0.5f);
    if (index < 0) index = 0;
    if (index > 9) index = 9;
    return index;
}

/****
 *
 * Draws CPU core usage visualization on the LED grid using spiral patterns
 *
 * DESCRIPTION:
 *   Renders individual CPU core usage as 3x3 spiral patterns on the left portion
 *   of the LED grid. Each CPU core gets its own spiral pattern with intensity
 *   based on usage percentage. Supports up to 8 CPU cores arranged in a 2x4 grid.
 *   Uses lookup table to determine which LEDs to illuminate for each usage level.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   cpu - Pointer to CPUValues containing per-core usage percentages
 *   fill_value - Integer brightness value (0-255) for illuminated LEDs
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies the LED grid buffer by setting pixel values for CPU visualization
 *
 * SECURITY FEATURES:
 *   - Null pointer validation for cpu->values and non-zero cpu->count
 *   - Bounds checking for grid coordinates to prevent buffer overruns
 *   - Core count limiting to prevent accessing beyond allocated CPU data
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; operates on provided grid and CPU data structures
 *
 ****/
// Layout cap. <=8 cores -> original 2x4 box layout in rows 0..16.
// 9..12 cores  -> extended 2x6 box layout in rows 0..24 (eats into mem/bat).
#define CPU_SPIRAL_CLASSIC_MAX  8
#define CPU_SPIRAL_EXTENDED_MAX 12

void draw_cpu(LEDGrid* grid, CPUValues* cpu, int fill_value) {
    if (!cpu->values || cpu->count == 0) {
        extended_layout = 0;
        return;
    }

    // Pick layout based on physical core count. Once set, other left-matrix
    // draw functions in this file consult `extended_layout`.
    extended_layout = (cpu->count > CPU_SPIRAL_CLASSIC_MAX);

    int total = cpu->count;
    int cap = extended_layout ? CPU_SPIRAL_EXTENDED_MAX : CPU_SPIRAL_CLASSIC_MAX;
    if (total > cap) total = cap;

    for (int i = 0; i < total; i++) {
        int column_number = i % 2;   // 0 = left half (grid_y 1..3), 1 = right (5..7)
        int row_number   = i / 2;    // 0..5 = top to bottom on the matrix

        int idx = spiral_index(cpu->values[i]);
        const int (*fill_grid)[3] = lookup_table[idx];

        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                int grid_y = 1 + column_number * 4 + y;   // 1..3 or 5..7
                int grid_x = 1 + row_number * 4 + x;      // 1..3, 5..7, ..., 21..23

                if (grid_x < GRID_WIDTH && grid_y < GRID_HEIGHT) {
                    if (fill_grid[y][x]) {
                        grid->grid[grid_y][grid_x] = fill_value;
                    }
                }
            }
        }
    }
}

/****
 *
 * Draws memory usage visualization as vertical bars on the LED grid
 *
 * DESCRIPTION:
 *   Renders system memory usage as two vertical columns of LEDs with height
 *   proportional to memory utilization. Uses a dual-column approach where
 *   both columns fill simultaneously but with slight offset for visual effect.
 *   Memory visualization appears in the middle section of the left LED matrix.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   mem - Pointer to MemoryValues containing usage percentage (0.0-1.0)
 *   fill_value - Integer brightness value (0-255) for illuminated LEDs
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies LED grid buffer by setting pixel values in columns 17-18
 *
 * SECURITY FEATURES:
 *   - Bounds checking for grid coordinates to prevent buffer overruns
 *   - Input validation ensures column indices are within grid width
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; operates on provided grid and memory data structures
 *
 ****/
void draw_memory(LEDGrid* grid, MemoryValues* mem, int fill_value) {
    // Classic layout (<=8 cores): memory at matrix cols 17/18.
    // Extended layout (>8 cores): memory shifted to cols 25/26.
    int col_top    = extended_layout ? 25 : 17;
    int col_bottom = extended_layout ? 26 : 18;

    float lit_pixels  = 7.0f * 2.0f * mem->usage_percent;
    int pixels_bottom = (int)roundf(lit_pixels / 2.0f);
    int pixels_top    = (int)roundf((lit_pixels - 0.49f) / 2.0f);

    for (int y = 1; y < 1 + pixels_top && y < GRID_HEIGHT; y++) {
        grid->grid[y][col_top] = fill_value;
    }
    for (int y = 1; y < 1 + pixels_bottom && y < GRID_HEIGHT; y++) {
        grid->grid[y][col_bottom] = fill_value;
    }
}

/****
 *
 * Draws battery status visualization with charging indicator on the LED grid
 *
 * DESCRIPTION:
 *   Renders battery charge level as horizontal bars filling from right to left
 *   across 7 rows. When battery is charging, overlays a lightning bolt pattern
 *   with enhanced brightness. Implements low battery protection by hiding
 *   visualization when battery is critically low and not charging.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   bat - Pointer to BatteryValues containing charge percentage and charging status
 *   fill_value - Integer brightness value (0-255) for base battery visualization
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies LED grid buffer in battery area (rows 1-7, columns 20-33)
 *
 * SECURITY FEATURES:
 *   - Bounds checking for all grid coordinate access
 *   - Battery level validation prevents division by zero
 *   - Brightness clamping prevents value overflow beyond 255
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; uses static lightning bolt pattern array
 *
 ****/
void draw_battery(LEDGrid* grid, BatteryValues* bat, int fill_value) {
    // Classic: battery occupies 13 vertical cols (20..32), 7 horizontal rows.
    // Extended: battery shrinks to 5 vertical cols (28..32) so the bigger CPU
    // section above can fit. Lightning bolt scales with the chosen layout.
    int width      = extended_layout ? 5  : 13;
    int start_col  = extended_layout ? 28 : 20;
    int end_col    = start_col + width;     // exclusive (also == col index of bottom outer border = 33)

    if (bat->percent <= 0.07f && !bat->is_charging) {
        return;  // Critically low and not charging: skip drawing.
    }

    int lit_pixels  = (int)roundf((float)width * 7.0f * bat->percent);
    int pixels_base = lit_pixels / 7;
    int remainder   = lit_pixels % 7;

    for (int i = 0; i < 7; i++) {
        int pixels_col = pixels_base + (i < remainder ? 1 : 0);
        int row = i + 1;
        if (row >= GRID_HEIGHT) continue;
        // Fill from the outer (right) edge of the section leftward, but never
        // cross the section's left border.
        int x_start = end_col - pixels_col;
        if (x_start < start_col) x_start = start_col;
        for (int x = x_start; x < end_col; x++) {
            grid->grid[row][x] = fill_value;
        }
    }

    if (bat->is_charging) {
        if (extended_layout) {
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 5; c++) {
                    if (!lightning_bolt_compact[r][c]) continue;
                    int grid_row = 1 + r;
                    int grid_col = start_col + c;
                    if (grid_row < GRID_HEIGHT && grid_col < GRID_WIDTH) {
                        int v = fill_value + 30;
                        if (v > 255) v = 255;
                        grid->grid[grid_row][grid_col] = v;
                    }
                }
            }
        } else {
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 13; c++) {
                    if (!lightning_bolt_classic[r][c]) continue;
                    int grid_row = 1 + r;
                    int grid_col = start_col + c;
                    if (grid_row < GRID_HEIGHT && grid_col < GRID_WIDTH) {
                        int v = fill_value + 30;
                        if (v > 255) v = 255;
                        grid->grid[grid_row][grid_col] = v;
                    }
                }
            }
        }
    }
}

/****
 *
 * Draws a generic percentage bar visualization on the LED grid
 *
 * DESCRIPTION:
 *   Renders a horizontal bar with configurable width, height, and position
 *   to visualize percentage values. Bar can fill from left-to-right or
 *   right-to-left depending on at_bottom parameter. Used for disk I/O
 *   and network traffic visualization on the right LED matrix.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   percent - Float value (0.0-1.0) representing the percentage to visualize
 *   fill_value - Integer brightness value (0-255) for illuminated LEDs
 *   x_offset - Integer row offset for bar positioning
 *   at_bottom - Boolean flag: 1 for right-to-left fill, 0 for left-to-right
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies LED grid buffer in the specified bar region
 *
 * SECURITY FEATURES:
 *   - Bounds checking for all grid coordinate calculations
 *   - Row offset validation prevents accessing invalid grid rows
 *   - Column range validation ensures safe array access
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; operates on provided grid structure
 *
 ****/
void draw_bar(LEDGrid* grid, float percent, int fill_value, int x_offset, int at_bottom) {
    // Match Python exactly:
    int bar_width = 3;
    int bar_height = 16;
    int lit_pixels = (int)roundf(bar_height * bar_width * percent);
    int pixels_base = lit_pixels / bar_width;
    int remainder = lit_pixels % bar_width;
    
    for (int i = 0; i < bar_width; i++) {
        int pixels_col = pixels_base;
        if (i < remainder) {
            pixels_col += 1;
        }
        
        int row = x_offset + i;  // Note: bar_x_offset affects row in Python!
        if (row >= 0 && row < GRID_HEIGHT) {
            if (at_bottom) {
                // grid[bar_x_offset+i, 33-pixels_col:33] = bar_value
                for (int x = 33 - pixels_col; x < 33 && x >= 0 && x < GRID_WIDTH; x++) {
                    grid->grid[row][x] = fill_value;
                }
            } else {
                // grid[bar_x_offset+i, 1:1+pixels_col] = bar_value
                for (int x = 1; x < 1 + pixels_col && x < GRID_WIDTH; x++) {
                    grid->grid[row][x] = fill_value;
                }
            }
        }
    }
}

/****
 *
 * Draws border lines and section dividers for the left LED matrix
 *
 * DESCRIPTION:
 *   Renders the structural border lines that separate different visualization
 *   areas on the left LED matrix. Creates partitions for CPU cores, memory
 *   usage, and battery sections with consistent visual separation. Includes
 *   outer perimeter borders and internal section dividers.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   value - Integer brightness value (0-255) for border illumination
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies LED grid buffer by drawing border lines across multiple regions
 *
 * SECURITY FEATURES:
 *   - Implicit bounds checking through loop constraints
 *   - Grid dimensions used as natural bounds for drawing operations
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; operates directly on provided grid structure
 *
 ****/
void draw_borders_left(LEDGrid* grid, int value) {
    // Vertical center divider runs the length of the CPU section. In the
    // classic layout it stops at row 15 (4 cell rows); in the extended
    // layout it continues to row 23 (6 cell rows).
    int divider_end = extended_layout ? 24 : 16;
    for (int x = 0; x < divider_end; x++) {
        grid->grid[4][x] = value;
    }

    // Horizontal partitions:
    //   classic:  4, 8, 12, 16 (between CPU cell rows), 19 (mem/bat border)
    //   extended: 4, 8, 12, 16, 20 (between CPU cell rows), 24 (CPU bottom),
    //             27 (mem/bat border)
    if (extended_layout) {
        static const int hborders[] = {4, 8, 12, 16, 20, 24, 27};
        for (size_t k = 0; k < sizeof(hborders)/sizeof(hborders[0]); k++) {
            int x = hborders[k];
            for (int y = 0; y < GRID_HEIGHT; y++) {
                grid->grid[y][x] = value;
            }
        }
    } else {
        static const int hborders[] = {4, 8, 12, 16, 19};
        for (size_t k = 0; k < sizeof(hborders)/sizeof(hborders[0]); k++) {
            int x = hborders[k];
            for (int y = 0; y < GRID_HEIGHT; y++) {
                grid->grid[y][x] = value;
            }
        }
    }

    // Outer frame (identical in both layouts).
    for (int x = 0; x < GRID_WIDTH; x++) {
        grid->grid[0][x] = value;
        grid->grid[8][x] = value;
    }
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][0]  = value;
        grid->grid[y][33] = value;
    }
}

/****
 *
 * Draws border lines and section dividers for the right LED matrix
 *
 * DESCRIPTION:
 *   Renders the structural border lines that separate different visualization
 *   areas on the right LED matrix. Creates partitions for disk I/O and
 *   network traffic sections with outer perimeter borders and a central
 *   horizontal divider to separate upper and lower display regions.
 *
 * PARAMETERS:
 *   grid - Pointer to LEDGrid structure containing the display buffer
 *   value - Integer brightness value (0-255) for border illumination
 *
 * RETURNS:
 *   None (void function)
 *
 * SIDE EFFECTS:
 *   Modifies LED grid buffer by drawing border lines in specified regions
 *
 * SECURITY FEATURES:
 *   - Implicit bounds checking through loop constraints
 *   - Grid dimensions used as natural bounds for drawing operations
 *
 * MEMORY MANAGEMENT:
 *   No dynamic allocation; operates directly on provided grid structure
 *
 ****/
void draw_borders_right(LEDGrid* grid, int value) {
    // Match Python exactly:
    // grid[:, 16] = border_value   (Middle Partition borders)
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][16] = value;
    }
    
    // grid[4, :] = border_value
    for (int x = 0; x < GRID_WIDTH; x++) {
        grid->grid[4][x] = value;
    }
    
    // grid[:, 0] = border_value    (Top outer edge)
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][0] = value;
    }
    
    // grid[0, :] = border_value    (Left outer edge)
    for (int x = 0; x < GRID_WIDTH; x++) {
        grid->grid[0][x] = value;
    }
    
    // grid[8, :] = border_value    (Right outer edge)
    for (int x = 0; x < GRID_WIDTH; x++) {
        grid->grid[8][x] = value;
    }
    
    // grid[:, 33] = border_value   (Bottom outer edge)
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][33] = value;
    }
}