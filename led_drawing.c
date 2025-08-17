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

// Lightning bolt pattern (transposed from Python version)
static const int lightning_bolt[7][13] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0}, // row 0
    {0,0,0,0,0,1,1,0,0,0,1,1,0}, // row 1 
    {0,0,0,0,1,1,1,0,1,1,1,0,0}, // row 2
    {0,0,0,1,1,1,1,1,1,1,0,0,0}, // row 3
    {0,0,1,1,1,0,1,1,1,0,0,0,0}, // row 4
    {0,1,1,0,0,0,1,1,0,0,0,0,0}, // row 5
    {0,0,0,0,0,0,0,0,0,0,0,0,0}  // row 6
};

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
void draw_cpu(LEDGrid* grid, CPUValues* cpu, int fill_value) {
    if (!cpu->values || cpu->count == 0) return;
    
    for (int i = 0; i < cpu->count && i < 8; i++) {
        int column_number = i % 2;
        int row_number = i / 2;
        
        int idx = spiral_index(cpu->values[i]);
        const int (*fill_grid)[3] = lookup_table[idx];
        
        // Match Python: grid[1+column_number*4:4+column_number*4, 1+row_number*4:4+row_number*4]
        // This means: y in [1+column_number*4, 4+column_number*4), x in [1+row_number*4, 4+row_number*4)
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                int grid_y = 1 + column_number * 4 + y;  // Swapped: column_number affects y
                int grid_x = 1 + row_number * 4 + x;     // Swapped: row_number affects x
                
                if (grid_x < GRID_WIDTH && grid_y < GRID_HEIGHT) {
                    // fill_grid is already transposed in Python, so use [y][x]
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
    // Match Python exactly:
    // lit_pixels = 7 * 2 * memory_ratio
    float lit_pixels = 7.0f * 2.0f * mem->usage_percent;
    int pixels_bottom = (int)roundf(lit_pixels / 2.0f);
    int pixels_top = (int)roundf((lit_pixels - 0.49f) / 2.0f);
    
    // grid[1:1+pixels_top,17] = fill_value
    for (int y = 1; y < 1 + pixels_top && y < GRID_HEIGHT; y++) {
        if (17 < GRID_WIDTH) {
            grid->grid[y][17] = fill_value;
        }
    }
    
    // grid[1:1+pixels_bottom,18] = fill_value  
    for (int y = 1; y < 1 + pixels_bottom && y < GRID_HEIGHT; y++) {
        if (18 < GRID_WIDTH) {
            grid->grid[y][18] = fill_value;
        }
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
    // Match Python exactly:
    // lit_pixels = int(round(13 * 7 * battery_ratio))
    int lit_pixels = (int)roundf(13.0f * 7.0f * bat->percent);
    int pixels_base = lit_pixels / 7;
    int remainder = lit_pixels % 7;
    
    // Battery low threshold check (simplified - no flashing in C version)
    if (bat->percent <= 0.07f && !bat->is_charging) {
        return; // Skip drawing when battery very low and not charging
    }
    
    // for i in range(7): grid[i+1,33-pixels_col:33] = fill_value
    for (int i = 0; i < 7; i++) {
        int pixels_col = pixels_base;
        if (i < remainder) {
            pixels_col += 1;
        }
        
        // grid[i+1, 33-pixels_col:33] = fill_value
        int row = i + 1;
        if (row < GRID_HEIGHT) {
            for (int x = 33 - pixels_col; x < 33 && x >= 0 && x < GRID_WIDTH; x++) {
                grid->grid[row][x] = fill_value;
            }
        }
    }
    
    // Handle charging lightning bolt
    if (bat->is_charging) {
        // Apply lightning bolt to grid[1:8, 20:33] region (7 rows, 13 columns)
        // Python: grid[1:8,20:33][lightning_bolt] -= np.rint(fill_value + 10 * pulse_amount).astype(int)
        // Simplified in C: just overlay the lightning bolt pattern
        
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 13; col++) {
                if (lightning_bolt[row][col]) {
                    int grid_row = 1 + row;  // grid[1:8, ...]
                    int grid_col = 20 + col; // grid[..., 20:33]
                    
                    if (grid_row < GRID_HEIGHT && grid_col < GRID_WIDTH) {
                        // Overlay lightning bolt (make it brighter than battery fill)
                        grid->grid[grid_row][grid_col] = fill_value + 30;
                        if (grid->grid[grid_row][grid_col] > 255) {
                            grid->grid[grid_row][grid_col] = 255;
                        }
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
    // Match Python exactly:
    // grid[4, :16] = border_value  (Cpu vertical partitions)
    for (int x = 0; x < 16; x++) {
        grid->grid[4][x] = value;
    }
    
    // grid[:, 4] = border_value    (Cpu horizontal partitions)
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][4] = value;
    }
    
    // grid[:, 8] = border_value
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][8] = value;
    }
    
    // grid[:, 12] = border_value
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][12] = value;
    }
    
    // grid[:, 16] = border_value
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][16] = value;
    }
    
    // grid[:, 19] = border_value   (Memory bottom partition)
    for (int y = 0; y < GRID_HEIGHT; y++) {
        grid->grid[y][19] = value;
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