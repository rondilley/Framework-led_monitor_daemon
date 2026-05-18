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
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>
#include <syslog.h>
#include "led_monitor.h"

// CLOCK_MONOTONIC in microseconds. Used for I/O rate timing: time(NULL)
// only has second resolution, which loses every disk/network sample at our
// 10Hz poll rate except the rare pair that straddles a second boundary.
static uint64_t monotonic_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

#define MAX_CPU_CORES 256
#define HISTORY_SIZE 10

typedef struct {
    unsigned long user;
    unsigned long nice;
    unsigned long system;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    unsigned long steal;
} CPUStat;

typedef struct {
    unsigned long long read_bytes;
    unsigned long long write_bytes;
    uint64_t timestamp_usec;
} DiskStat;

typedef struct {
    unsigned long long sent_bytes;
    unsigned long long recv_bytes;
    uint64_t timestamp_usec;
} NetworkStat;

static CPUStat* prev_cpu_stats = NULL;   // indexed by physical core
static int cpu_count = 0;                 // physical core count
static int total_logical = 0;             // total logical CPUs in /proc/stat
static int* logical_to_physical = NULL;   // logical CPU id -> physical idx, or -1
static float** cpu_history = NULL;
static int* cpu_history_index = NULL;

static DiskStat disk_history[20];
static int disk_history_index = 0;
static int disk_history_count = 0;
static float highest_disk_read_rate = 0.00001;
static float highest_disk_write_rate = 0.00001;

// True for whole-disk device names; false for partitions and non-disk
// entries. Replaces the old `(major == 8 || major == 259) && minor%16 == 0`
// filter, which was a SCSI-allocation heuristic and missed NVMe namespaces
// (major 259 doesn't follow the 16-minors-per-disk convention).
//
// Recognized whole-disk patterns:
//   sd[a-z]+, vd[a-z]+, hd[a-z]+, xvd[a-z]+   (trailing char is alpha)
//   nvme<N>n<M>                                (no "p<part>" suffix)
//   mmcblk<N>                                  (no "p<part>" suffix)
static int is_whole_disk(const char* name) {
    size_t len = strlen(name);
    if (len == 0) return 0;

    if (strncmp(name, "nvme", 4) == 0) {
        const char* p = name + 4;
        if (!(*p >= '0' && *p <= '9')) return 0;
        while (*p >= '0' && *p <= '9') p++;
        if (*p != 'n') return 0;
        p++;
        if (!(*p >= '0' && *p <= '9')) return 0;
        while (*p >= '0' && *p <= '9') p++;
        return *p == '\0';
    }
    if (strncmp(name, "mmcblk", 6) == 0) {
        const char* p = name + 6;
        if (!(*p >= '0' && *p <= '9')) return 0;
        while (*p >= '0' && *p <= '9') p++;
        return *p == '\0';
    }
    if (strncmp(name, "xvd", 3) == 0 ||
        strncmp(name, "sd",  2) == 0 ||
        strncmp(name, "vd",  2) == 0 ||
        strncmp(name, "hd",  2) == 0) {
        char last = name[len - 1];
        return !(last >= '0' && last <= '9');
    }
    return 0;
}

static NetworkStat network_history[20];
static int network_history_index = 0;
static int network_history_count = 0;
static float highest_network_sent_rate = 0.00001;
static float highest_network_recv_rate = 0.00001;

/****
 *
 * Initialize CPU monitoring system with dynamic core detection and history tracking
 *
 * DESCRIPTION:
 *   Dynamically detects the number of CPU cores by parsing /proc/stat and initializes
 *   internal data structures for CPU usage tracking. Allocates memory for per-core
 *   statistics history and previous state tracking. Handles hyperthreading by counting
 *   only physical cores (dividing detected cores by 2).
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   void - No return value
 *
 * SIDE EFFECTS:
 *   - Allocates memory for prev_cpu_stats, cpu_history, and cpu_history_index arrays
 *   - Sets global cpu_count variable
 *   - Opens and reads /proc/stat file
 *
 * SECURITY FEATURES:
 *   - Uses calloc for zero-initialized memory allocation
 *   - Validates file operations before proceeding
 *   - Bounds checking with MAX_CPU_CORES limit
 *
 * MEMORY MANAGEMENT:
 *   - Allocates cpu_count * sizeof(CPUStat) for prev_cpu_stats
 *   - Allocates cpu_count * sizeof(float*) for cpu_history array
 *   - Allocates cpu_count * HISTORY_SIZE * sizeof(float) for history data
 *   - Memory persists for application lifetime (static variables)
 *
 ****/
static int read_core_id(int cpu) {
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;
    int id = -1;
    if (fscanf(fp, "%d", &id) != 1) id = -1;
    fclose(fp);
    return id;
}

static int read_physical_package_id(int cpu) {
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    int id = 0;
    if (fscanf(fp, "%d", &id) != 1) id = 0;
    fclose(fp);
    return id;
}

static void init_cpu_monitoring() {
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return;

    char line[256];
    total_logical = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            total_logical++;
        }
    }
    fclose(fp);

    if (total_logical <= 0 || total_logical > MAX_CPU_CORES) {
        total_logical = 0;
        return;
    }

    logical_to_physical = calloc(total_logical, sizeof(int));
    if (!logical_to_physical) return;
    for (int i = 0; i < total_logical; i++) logical_to_physical[i] = -1;

    // Group logical CPUs into physical cores using (package_id, core_id).
    // The lowest-numbered logical CPU in each group represents the core.
    int seen_pkg[MAX_CPU_CORES];
    int seen_core[MAX_CPU_CORES];
    int seen_count = 0;

    for (int cpu = 0; cpu < total_logical; cpu++) {
        int cid = read_core_id(cpu);
        int pid = read_physical_package_id(cpu);
        if (cid < 0) cid = cpu;  // Fallback: treat each logical CPU as physical.

        int phys_idx = -1;
        for (int i = 0; i < seen_count; i++) {
            if (seen_core[i] == cid && seen_pkg[i] == pid) {
                phys_idx = i;
                break;
            }
        }
        if (phys_idx < 0) {
            phys_idx = seen_count;
            seen_pkg[seen_count] = pid;
            seen_core[seen_count] = cid;
            seen_count++;
            logical_to_physical[cpu] = phys_idx;
        }
        // Sibling threads: left as -1, so they are skipped in stats reads.
    }

    cpu_count = seen_count;

    if (cpu_count > 0) {
        prev_cpu_stats = calloc(cpu_count, sizeof(CPUStat));
        cpu_history = calloc(cpu_count, sizeof(float*));
        cpu_history_index = calloc(cpu_count, sizeof(int));

        for (int i = 0; i < cpu_count; i++) {
            cpu_history[i] = calloc(HISTORY_SIZE, sizeof(float));
        }
    }

    syslog(LOG_INFO, "CPU monitoring: %d logical CPUs, %d physical cores",
           total_logical, cpu_count);
}

/****
 *
 * Retrieve real-time CPU usage percentages for all detected cores with historical smoothing
 *
 * DESCRIPTION:
 *   Calculates CPU usage by comparing current /proc/stat values with previous readings.
 *   Implements a rolling average over HISTORY_SIZE samples to smooth out usage spikes.
 *   Handles hyperthreading by processing only even-numbered CPU entries from /proc/stat.
 *   Automatically initializes monitoring system on first call.
 *
 * PARAMETERS:
 *   cpu - Pointer to CPUValues structure to populate with usage data
 *
 * RETURNS:
 *   void - Results stored in cpu->values array, cpu->count set to number of cores
 *
 * SIDE EFFECTS:
 *   - Updates global prev_cpu_stats with current readings
 *   - Advances cpu_history_index for each core
 *   - May trigger init_cpu_monitoring() on first call
 *   - Reallocates cpu->values if core count changes
 *
 * SECURITY FEATURES:
 *   - Validates file operations and handles failures gracefully
 *   - Bounds checking on array indices and core counts
 *   - Sanitizes usage calculations to [0.0, 1.0] range
 *
 * MEMORY MANAGEMENT:
 *   - May free and reallocate cpu->values if core count differs
 *   - Uses calloc for zero-initialized allocation
 *   - Caller responsible for freeing cpu->values
 *
 ****/
void get_cpu_values(CPUValues* cpu) {
    if (!prev_cpu_stats) {
        init_cpu_monitoring();
        if (!prev_cpu_stats) {
            cpu->count = 0;
            return;
        }
    }
    
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) {
        cpu->count = 0;
        return;
    }
    
    if (!cpu->values || cpu->count != cpu_count) {
        if (cpu->values) free(cpu->values);
        cpu->values = calloc(cpu_count, sizeof(float));
        cpu->count = cpu_count;
    }
    
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            int cpu_num = atoi(&line[3]);

            if (cpu_num < 0 || cpu_num >= total_logical) continue;
            int idx = logical_to_physical[cpu_num];
            if (idx < 0 || idx >= cpu_count) continue;

            {
                CPUStat curr;
                sscanf(line, "cpu%*d %lu %lu %lu %lu %lu %lu %lu %lu",
                       &curr.user, &curr.nice, &curr.system, &curr.idle,
                       &curr.iowait, &curr.irq, &curr.softirq, &curr.steal);
                
                unsigned long prev_total = prev_cpu_stats[idx].user + prev_cpu_stats[idx].nice +
                                          prev_cpu_stats[idx].system + prev_cpu_stats[idx].idle +
                                          prev_cpu_stats[idx].iowait + prev_cpu_stats[idx].irq +
                                          prev_cpu_stats[idx].softirq + prev_cpu_stats[idx].steal;
                
                unsigned long curr_total = curr.user + curr.nice + curr.system + curr.idle +
                                          curr.iowait + curr.irq + curr.softirq + curr.steal;
                
                unsigned long total_diff = curr_total - prev_total;
                unsigned long idle_diff = curr.idle - prev_cpu_stats[idx].idle;
                
                float usage = 0.0;
                if (total_diff > 0) {
                    usage = 1.0 - ((float)idle_diff / (float)total_diff);
                    if (usage < 0.0) usage = 0.0;
                    if (usage > 1.0) usage = 1.0;
                }
                
                cpu_history[idx][cpu_history_index[idx]] = usage;
                cpu_history_index[idx] = (cpu_history_index[idx] + 1) % HISTORY_SIZE;
                
                float sum = 0.0;
                for (int i = 0; i < HISTORY_SIZE; i++) {
                    sum += cpu_history[idx][i];
                }
                cpu->values[idx] = sum / HISTORY_SIZE;
                
                prev_cpu_stats[idx] = curr;
            }
        }
    }
    
    fclose(fp);
}

/****
 *
 * Retrieve current system memory usage as a percentage of total available memory
 *
 * DESCRIPTION:
 *   Reads memory statistics from /proc/meminfo and calculates usage percentage
 *   based on MemTotal and MemAvailable fields. MemAvailable accounts for reclaimable
 *   memory including buffers and cached data, providing more accurate available memory
 *   estimation than simple MemFree calculations.
 *
 * PARAMETERS:
 *   mem - Pointer to MemoryValues structure to populate with usage data
 *
 * RETURNS:
 *   void - Sets mem->usage_percent to value between 0.0 and 1.0
 *
 * SIDE EFFECTS:
 *   - Opens and reads /proc/meminfo file
 *   - No persistent state changes
 *
 * SECURITY FEATURES:
 *   - Validates file operations and handles failures gracefully
 *   - Division by zero protection when mem_total is 0
 *   - Input validation on parsed memory values
 *
 * MEMORY MANAGEMENT:
 *   - Uses stack-allocated buffers for file I/O
 *   - No dynamic memory allocation
 *   - Automatic cleanup via stack unwinding
 *
 ****/
void get_memory_values(MemoryValues* mem) {
    FILE* fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        mem->usage_percent = 0.0;
        return;
    }
    
    char line[256];
    unsigned long mem_total = 0, mem_available = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
            continue;
        }
        if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
            break;
        }
    }
    fclose(fp);
    
    if (mem_total > 0) {
        mem->usage_percent = 1.0 - ((float)mem_available / (float)mem_total);
    } else {
        mem->usage_percent = 0.0;
    }
}

/****
 *
 * Retrieve battery charge percentage and charging status from system power supply
 *
 * DESCRIPTION:
 *   Scans /sys/class/power_supply/ directory for battery devices (BAT prefix) and
 *   reads capacity and status information. Handles systems with multiple batteries
 *   by using the first detected battery. Falls back to full charge and charging
 *   status if no battery is detected (desktop systems).
 *
 * PARAMETERS:
 *   bat - Pointer to BatteryValues structure to populate with battery data
 *
 * RETURNS:
 *   void - Sets bat->percent (0.0-1.0) and bat->is_charging (0 or 1)
 *
 * SIDE EFFECTS:
 *   - Opens and reads multiple files in /sys/class/power_supply/
 *   - Directory traversal of power supply devices
 *
 * SECURITY FEATURES:
 *   - Path construction with bounds checking using snprintf
 *   - Validates file operations before proceeding
 *   - Graceful handling of missing battery devices
 *
 * MEMORY MANAGEMENT:
 *   - Uses stack-allocated buffers for file paths and data
 *   - No dynamic memory allocation
 *   - Automatic resource cleanup via file handle management
 *
 ****/
void get_battery_values(BatteryValues* bat) {
    const char* power_supply_path = "/sys/class/power_supply/";
    DIR* dir = opendir(power_supply_path);
    if (!dir) {
        bat->percent = 0.0;
        bat->is_charging = 0;
        return;
    }
    
    struct dirent* entry;
    int found_battery = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "BAT", 3) == 0) {
            char path[512];
            FILE* fp;
            
            snprintf(path, sizeof(path), "%s%s/capacity", power_supply_path, entry->d_name);
            fp = fopen(path, "r");
            if (fp) {
                int capacity;
                if (fscanf(fp, "%d", &capacity) == 1) {
                    bat->percent = (float)capacity / 100.0;
                    found_battery = 1;
                }
                fclose(fp);
            }
            
            snprintf(path, sizeof(path), "%s%s/status", power_supply_path, entry->d_name);
            fp = fopen(path, "r");
            if (fp) {
                char status[32];
                if (fgets(status, sizeof(status), fp)) {
                    bat->is_charging = (strncmp(status, "Charging", 8) == 0);
                }
                fclose(fp);
            }
            
            if (found_battery) break;
        }
    }
    closedir(dir);
    
    if (!found_battery) {
        bat->percent = 1.0;
        bat->is_charging = 1;
    }
}

/****
 *
 * Calculate disk I/O activity percentages based on read/write rates with adaptive scaling
 *
 * DESCRIPTION:
 *   Monitors disk activity by parsing /proc/diskstats for physical disk devices
 *   (major numbers 8 and 259, minor multiples of 16). Maintains a 20-sample rolling
 *   history to calculate I/O rates over time. Implements adaptive scaling where
 *   the highest observed rates become 100% reference points for percentage calculations.
 *
 * PARAMETERS:
 *   disk - Pointer to DiskValues structure to populate with I/O percentages
 *
 * RETURNS:
 *   void - Sets disk->read_percent and disk->write_percent (0.0-1.0)
 *
 * SIDE EFFECTS:
 *   - Updates global disk_history circular buffer
 *   - Advances disk_history_index and disk_history_count
 *   - Updates highest_disk_read_rate and highest_disk_write_rate thresholds
 *
 * SECURITY FEATURES:
 *   - Validates major/minor device numbers to filter physical disks only
 *   - Bounds checking on history buffer operations
 *   - Time difference validation to prevent division by zero
 *
 * MEMORY MANAGEMENT:
 *   - Uses static arrays for history tracking (disk_history[20])
 *   - Stack-allocated buffers for file I/O operations
 *   - No dynamic memory allocation
 *
 ****/
void get_disk_values(DiskValues* disk) {
    FILE* fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        disk->read_percent = 0.0;
        disk->write_percent = 0.0;
        return;
    }
    
    char line[512];
    unsigned long long total_read_sectors = 0, total_write_sectors = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        char device[32];
        unsigned long long read_sectors, write_sectors;
        int major, minor;

        if (sscanf(line, "%d %d %31s %*u %*u %llu %*u %*u %*u %llu",
                   &major, &minor, device, &read_sectors, &write_sectors) == 5) {
            if (is_whole_disk(device)) {
                total_read_sectors += read_sectors;
                total_write_sectors += write_sectors;
            }
        }
    }
    fclose(fp);
    
    DiskStat current;
    current.read_bytes = total_read_sectors * 512;
    current.write_bytes = total_write_sectors * 512;
    current.timestamp_usec = monotonic_usec();

    // Decay the adaptive "highest seen" so a one-off burst doesn't pin the
    // scale forever. At ~10Hz, 0.9999 per call gives a half-life of ~70s.
    // Floor avoids divide-by-tiny that would render any I/O as 100%.
    const float DISK_RATE_FLOOR = 4096.0f;
    highest_disk_read_rate  *= 0.9999f;
    highest_disk_write_rate *= 0.9999f;
    if (highest_disk_read_rate  < DISK_RATE_FLOOR) highest_disk_read_rate  = DISK_RATE_FLOOR;
    if (highest_disk_write_rate < DISK_RATE_FLOOR) highest_disk_write_rate = DISK_RATE_FLOOR;

    if (disk_history_count > 0) {
        int oldest_idx = (disk_history_index - disk_history_count + 20) % 20;
        DiskStat* oldest = &disk_history[oldest_idx];

        uint64_t usec_diff = current.timestamp_usec - oldest->timestamp_usec;
        if (usec_diff > 0) {
            float read_rate  = (float)(current.read_bytes  - oldest->read_bytes)  * 1000000.0f / (float)usec_diff;
            float write_rate = (float)(current.write_bytes - oldest->write_bytes) * 1000000.0f / (float)usec_diff;

            if (read_rate > highest_disk_read_rate) {
                highest_disk_read_rate = read_rate;
            }
            if (write_rate > highest_disk_write_rate) {
                highest_disk_write_rate = write_rate;
            }

            disk->read_percent = fminf(1.0, read_rate / highest_disk_read_rate);
            disk->write_percent = fminf(1.0, write_rate / highest_disk_write_rate);
        } else {
            disk->read_percent = 0.0;
            disk->write_percent = 0.0;
        }
    } else {
        disk->read_percent = 0.0;
        disk->write_percent = 0.0;
    }
    
    disk_history[disk_history_index] = current;
    disk_history_index = (disk_history_index + 1) % 20;
    if (disk_history_count < 20) {
        disk_history_count++;
    }
}

/****
 *
 * Calculate network traffic percentages based on upload/download rates with adaptive scaling
 *
 * DESCRIPTION:
 *   Monitors network activity by parsing /proc/net/dev for all non-loopback interfaces.
 *   Maintains a 20-sample rolling history to calculate traffic rates over time.
 *   Implements adaptive scaling where the highest observed rates become 100% reference
 *   points. Excludes loopback interface to focus on external network activity.
 *
 * PARAMETERS:
 *   net - Pointer to NetworkValues structure to populate with traffic percentages
 *
 * RETURNS:
 *   void - Sets net->download_percent and net->upload_percent (0.0-1.0)
 *
 * SIDE EFFECTS:
 *   - Updates global network_history circular buffer
 *   - Advances network_history_index and network_history_count
 *   - Updates highest_network_recv_rate and highest_network_sent_rate thresholds
 *
 * SECURITY FEATURES:
 *   - Interface name validation to exclude loopback
 *   - Bounds checking on history buffer operations
 *   - Time difference validation to prevent division by zero
 *
 * MEMORY MANAGEMENT:
 *   - Uses static arrays for history tracking (network_history[20])
 *   - Stack-allocated buffers for file I/O and string parsing
 *   - No dynamic memory allocation
 *
 ****/
void get_network_values(NetworkValues* net) {
    FILE* fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        net->upload_percent = 0.0;
        net->download_percent = 0.0;
        return;
    }
    
    char line[512];
    unsigned long long total_recv_bytes = 0, total_sent_bytes = 0;
    
    // Skip header lines
    if (!fgets(line, sizeof(line), fp)) return;
    if (!fgets(line, sizeof(line), fp)) return;
    
    while (fgets(line, sizeof(line), fp)) {
        char* interface = strtok(line, ":");
        if (interface) {
            while (*interface == ' ') interface++;
            
            if (strncmp(interface, "lo", 2) != 0) {
                unsigned long long recv_bytes, sent_bytes;
                if (sscanf(strtok(NULL, ""), "%llu %*u %*u %*u %*u %*u %*u %*u %llu",
                          &recv_bytes, &sent_bytes) == 2) {
                    total_recv_bytes += recv_bytes;
                    total_sent_bytes += sent_bytes;
                }
            }
        }
    }
    fclose(fp);
    
    NetworkStat current;
    current.recv_bytes = total_recv_bytes;
    current.sent_bytes = total_sent_bytes;
    current.timestamp_usec = monotonic_usec();

    // Same decay strategy as disk: fade the adaptive max so a single burst
    // doesn't flatten the visualization forever.
    const float NET_RATE_FLOOR = 4096.0f;
    highest_network_recv_rate *= 0.9999f;
    highest_network_sent_rate *= 0.9999f;
    if (highest_network_recv_rate < NET_RATE_FLOOR) highest_network_recv_rate = NET_RATE_FLOOR;
    if (highest_network_sent_rate < NET_RATE_FLOOR) highest_network_sent_rate = NET_RATE_FLOOR;

    if (network_history_count > 0) {
        int oldest_idx = (network_history_index - network_history_count + 20) % 20;
        NetworkStat* oldest = &network_history[oldest_idx];

        uint64_t usec_diff = current.timestamp_usec - oldest->timestamp_usec;
        if (usec_diff > 0) {
            float recv_rate = (float)(current.recv_bytes - oldest->recv_bytes) * 1000000.0f / (float)usec_diff;
            float sent_rate = (float)(current.sent_bytes - oldest->sent_bytes) * 1000000.0f / (float)usec_diff;

            if (recv_rate > highest_network_recv_rate) {
                highest_network_recv_rate = recv_rate;
            }
            if (sent_rate > highest_network_sent_rate) {
                highest_network_sent_rate = sent_rate;
            }

            net->download_percent = fminf(1.0, recv_rate / highest_network_recv_rate);
            net->upload_percent = fminf(1.0, sent_rate / highest_network_sent_rate);
        } else {
            net->download_percent = 0.0;
            net->upload_percent = 0.0;
        }
    } else {
        net->download_percent = 0.0;
        net->upload_percent = 0.0;
    }
    
    network_history[network_history_index] = current;
    network_history_index = (network_history_index + 1) % 20;
    if (network_history_count < 20) {
        network_history_count++;
    }
}

/****
 *
 * Retrieve current screen brightness as a ratio of maximum brightness
 *
 * DESCRIPTION:
 *   Scans /sys/class/backlight/ directory for backlight devices and reads current
 *   brightness value relative to maximum brightness. Returns normalized ratio
 *   between 0.0 and 1.0. Uses the first available backlight device found.
 *   Falls back to full brightness (1.0) if no backlight devices are detected.
 *
 * PARAMETERS:
 *   None
 *
 * RETURNS:
 *   float - Brightness ratio between 0.0 (minimum) and 1.0 (maximum)
 *
 * SIDE EFFECTS:
 *   - Directory traversal of /sys/class/backlight/
 *   - Opens and reads brightness and max_brightness files
 *
 * SECURITY FEATURES:
 *   - Path construction with bounds checking using snprintf
 *   - Validates file operations and handles read failures
 *   - Division by zero protection when max_brightness is 0
 *
 * MEMORY MANAGEMENT:
 *   - Uses stack-allocated buffers for file paths
 *   - No dynamic memory allocation
 *   - Automatic resource cleanup via file handle management
 *
 ****/
float get_screen_brightness(void) {
    const char* backlight_path = "/sys/class/backlight/";
    DIR* dir = opendir(backlight_path);
    if (!dir) {
        return 1.0;
    }
    
    struct dirent* entry;
    float brightness_ratio = 1.0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_LNK || entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            char brightness_path[512];
            char max_brightness_path[512];
            FILE* fp;
            
            snprintf(brightness_path, sizeof(brightness_path), 
                    "%s%s/brightness", backlight_path, entry->d_name);
            snprintf(max_brightness_path, sizeof(max_brightness_path), 
                    "%s%s/max_brightness", backlight_path, entry->d_name);
            
            int brightness = 0, max_brightness = 0;
            
            fp = fopen(brightness_path, "r");
            if (fp) {
                if (fscanf(fp, "%d", &brightness) != 1) {
                    brightness = 0;
                }
                fclose(fp);
            }
            
            fp = fopen(max_brightness_path, "r");
            if (fp) {
                if (fscanf(fp, "%d", &max_brightness) != 1) {
                    max_brightness = 0;
                }
                fclose(fp);
            }
            
            if (max_brightness > 0) {
                brightness_ratio = (float)brightness / (float)max_brightness;
                break;
            }
        }
    }
    closedir(dir);
    
    return brightness_ratio;
}