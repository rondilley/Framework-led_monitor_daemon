# Framework LED Monitor Daemon

A high-performance C implementation of the Framework LED system monitor daemon with enterprise-grade features. This daemon displays real-time system metrics on Framework Laptop 16 LED input modules.

**Based on the original Python implementation by Jeremy Karst:**  
https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git

## Features

- **Multi-threaded architecture** for independent LED matrix control
- **Real-time system monitoring**:
  - CPU usage per physical core, with automatic layout switching for 1–12 cores (SMT siblings are folded into their physical cores via `/sys/devices/system/cpu/.../topology`)
  - Memory utilization
  - Battery status and charging state
  - Disk I/O (read/write)
  - Network traffic (upload/download)
- **Adaptive brightness** based on laptop display brightness
- **Automatic device detection** via USB path (`/dev/serial/by-path` then `/dev/serial/by-id` fallback)
- **Low resource usage** compared to Python version
- **Plain `key = value` configuration files** (system-wide and per-user)
- **Multi-level logging** (debug / info / warn / error) routed to syslog, plus stderr in foreground mode
- **Signal-based configuration reload** (SIGUSR1, SIGHUP)
- **Device detection self-test** (`--test-devices`)
- **Systemd service unit** for boot-time startup

## Requirements

- Linux system with /proc and /sys filesystems
- Framework Laptop 16 with LED input modules
- GCC compiler
- pthread library
- User in `dialout` group or appropriate udev rules for USB access

## Building

```bash
# Build the daemon
make

# Build with debug symbols
make debug

# Clean build files
make clean

# Build and test
make && ./ledmonitord --test-devices

# Check serial device permissions
make check-serial
```

### Serial Device Permissions

When you run `make`, it will automatically check the permissions for `/dev/ttyACM0` and inform you which group owns the device (typically `dialout`). The check will:
- Identify the group that owns the serial device
- Provide the exact command to add your user to that group
- Show whether your current user is already in the required group

If you see that you're not in the required group, follow the instructions provided to add yourself:
```bash
sudo usermod -a -G dialout $USER
# Then logout and login again for changes to take effect
```

## Installation

```bash
# Install daemon to /usr/local/bin
sudo make install

# Create framework service account
sudo useradd --system --no-create-home --gid dialout framework

# Copy service file to systemd
sudo cp led-monitor.service /etc/systemd/system/

# Reload systemd and enable service
sudo systemctl daemon-reload
sudo systemctl enable led-monitor

# Start the service
sudo systemctl start led-monitor

# Check service status
sudo systemctl status led-monitor
```

### Note on Systemd Hardening
The shipped unit uses `Type=forking` (the daemon double-forks itself) and runs as `User=framework`, `Group=dialout` so the worker threads can open `/dev/ttyACM*`. The unit currently sets only a few resource limits (`LimitNOFILE`, `MemoryMax`, `CPUQuota`) and does **not** enable directives such as `NoNewPrivileges`, `ProtectSystem`, `ProtectHome`, `PrivateTmp`, or `SystemCallFilter`. If you want a tighter sandbox, add those directives to `led-monitor.service` and verify the daemon still starts (the double-fork can interact badly with some hardening flags).

### Configuration

No config file is required — the daemon uses built-in defaults if neither config file exists. To override, create one of:

```bash
# System-wide
sudo $EDITOR /etc/led_monitor.conf

# Per-user (takes effect only if the system-wide file is absent)
mkdir -p ~/.config && $EDITOR ~/.config/led_monitor.conf
```

See [Configuration Options](#configuration-options) below for recognized keys.

## Usage

```bash
# Run in foreground (for testing)
./ledmonitord --foreground

# Run with debug logging
./ledmonitord --foreground --debug

# Test device detection
./ledmonitord --test-devices

# Run with custom config
./ledmonitord --config /path/to/config.conf

# Show help
./ledmonitord --help

# Quick test
make run
```

### Systemd Service Management
```bash
# Start service
sudo systemctl start led-monitor

# Stop service
sudo systemctl stop led-monitor

# Enable auto-start at boot
sudo systemctl enable led-monitor

# Reload configuration
sudo systemctl reload led-monitor

# View logs
sudo journalctl -u led-monitor -f

# Check service health
sudo systemctl status led-monitor
```

## Uninstallation

```bash
# Remove daemon and service
sudo make uninstall
```

## LED Layout and Visualization

The Framework LED matrices display real-time system metrics using a carefully designed layout that maximizes information density while remaining readable at a glance.

### Matrix Configuration
- **Left Matrix** (USB 1-4.2): System cores, memory, and power
- **Right Matrix** (USB 1-3.3): Storage and network activity
- **Resolution**: 9×34 pixels per matrix (306 LEDs total per side)
- **Update Rate**: 10Hz (100ms refresh cycle)
- **Brightness**: Adaptive based on laptop display brightness

### Left Matrix Layout

The left matrix has two layouts. The daemon picks one automatically at runtime based on the physical-core count reported by `/sys/devices/system/cpu/.../topology`:

| Physical cores | Layout   | CPU section | Memory  | Battery |
|----------------|----------|-------------|---------|---------|
| 1 – 8          | Classic  | rows 1–15   | rows 17–18 | rows 20–32 (13 wide, full bolt) |
| 9 – 12         | Extended | rows 1–23   | rows 25–26 | rows 28–32 (5 wide, compact bolt) |

Cores beyond 12 are not rendered; SMT siblings are merged into their physical core.

#### Classic Layout (≤8 physical cores)

```
     Left LED Matrix (9×34)
     USB Port: 1-4.2
  0 ┌───────────────┐
  1 │ x x x | x x x | CPU Core Usage
  2 │ x x x | x x x | (up to 8 cores, 2×4 grid of 3×3 cells)
  3 │ x x x | x x x | Each cell = 1 physical core
  4 │───────|───────| Fill = % usage (spiral pattern)
  5 │ x x x | x x x |
  6 │ x x x | x x x |
  7 │ x x x | x x x |
  8 │───────|───────|
  9 │ x x x | x x x |
 10 │ x x x | x x x |
 11 │ x x x | x x x |
 12 │───────|───────|
 13 │ x x x | x x x |
 14 │ x x x | x x x |
 15 │ x x x | x x x |
 16 │───────────────│
 17 │ x x x x x x x │ Memory Usage Bar
 18 │ x x x x x x x │ Width = % of total RAM
 19 │───────────────│
 20 │ x x x x x x x │ Battery Level
 21 │ x x x x x x x │
 22 │ x x x x x x x │ ⚡ = charging (7×13 lightning bolt)
 23 │ x x x x x x x │ Segments fill from right edge = % charge
 24 │ x x x x x x x │
 25 │ x x x x x x x │
 26 │ x x x x x x x │
 27 │ x x x x x x x │
 28 │ x x x x x x x │
 29 │ x x x x x x x │
 30 │ x x x x x x x │
 31 │ x x x x x x x │
 32 │ x x x x x x x │
 33 └───────────────┘
```

#### Extended Layout (9–12 physical cores)

The CPU section grows from 8 to 12 cells by adding two more rows of 3×3 cells. To make room, the memory bar is compressed from 2 cols × 7 rows of usable space into the same 2 cols but shifted down, and the battery section shrinks from 13 rows to 5 rows with a compact 7×5 lightning bolt.

```
     Left LED Matrix (9×34)
     USB Port: 1-4.2
  0 ┌───────────────┐
  1 │ x x x | x x x | CPU Core Usage
  2 │ x x x | x x x | (up to 12 cores, 2×6 grid of 3×3 cells)
  3 │ x x x | x x x | Each cell = 1 physical core
  4 │───────|───────|
  5 │ x x x | x x x |
  6 │ x x x | x x x |
  7 │ x x x | x x x |
  8 │───────|───────|
  9 │ x x x | x x x |
 10 │ x x x | x x x |
 11 │ x x x | x x x |
 12 │───────|───────|
 13 │ x x x | x x x |
 14 │ x x x | x x x |
 15 │ x x x | x x x |
 16 │───────|───────|
 17 │ x x x | x x x |
 18 │ x x x | x x x |
 19 │ x x x | x x x |
 20 │───────|───────|
 21 │ x x x | x x x |
 22 │ x x x | x x x |
 23 │ x x x | x x x |
 24 │───────────────│
 25 │ x x x x x x x │ Memory Usage Bar (shifted down)
 26 │ x x x x x x x │
 27 │───────────────│
 28 │ x x x x x x x │ Battery Level (compact 5-row section)
 29 │ x x x x x x x │ ⚡ = charging (7×5 compact bolt)
 30 │ x x x x x x x │ Segments fill from right edge = % charge
 31 │ x x x x x x x │
 32 │ x x x x x x x │
 33 └───────────────┘
```

### Right Matrix Layout

```
     Right LED Matrix (9×34)
     USB Port: 1-3.3
  0 ┌───────────────┐
  1 │ x x x | x x x | Disk Usage
  2 │ x x x | x x x | Left column = Read
  3 │ x x x | x x x | Right column = Write
  4 │ x x x | x x x | Height = % usage
  5 │ x x x | x x x |
  6 │ x x x | x x x |
  7 │ x x x | x x x |
  8 │ x x x | x x x |
  9 │ x x x | x x x |
 10 │ x x x | x x x |
 11 │ x x x | x x x |
 12 │ x x x | x x x |
 13 │ x x x | x x x |
 14 │ x x x | x x x |
 15 │ x x x | x x x |
 16 │───────────────|
 17 │ x x x | x x x │ Network Usage
 18 │ x x x | x x x │ Left column = Upload
 19 │ x x x | x x x │ Right column = Download
 20 │ x x x | x x x │ Height = % usage
 21 │ x x x | x x x │ 
 22 │ x x x | x x x │ 
 23 │ x x x | x x x │ 
 24 │ x x x | x x x │ 
 25 │ x x x | x x x │ 
 26 │ x x x | x x x │ 
 27 │ x x x | x x x │ 
 28 │ x x x | x x x │ 
 29 │ x x x | x x x │ 
 30 │ x x x | x x x │ 
 31 │ x x x | x x x │ 
 32 │ x x x | x x x │ 
 33 └───────────────┘
```

### Visualization Details

#### CPU Cores (Left Matrix)
- **Detection**: Physical cores are detected from `/sys/devices/system/cpu/cpuN/topology/core_id` and `physical_package_id`; SMT/hyperthreaded siblings are folded into their parent physical core so each cell represents one real core.
- **Layout**: 3×3 cells arranged in 2 columns. The grid grows downward with core count:
  - **1–8 cores (Classic)**: 2×4 grid spanning rows 1–15
  - **9–12 cores (Extended)**: 2×6 grid spanning rows 1–23
- **Per-cell fill**: A spiral lookup pattern fills 0–9 of the 9 sub-pixels in each 3×3 cell, proportional to that core's usage (0–100%).
- **Layout switching**: Automatic at startup based on the detected physical core count; the choice also drives where the memory bar and battery section appear.

#### Memory Usage (Left Matrix)
- **Layout**: Two narrow vertical columns in the strip between the CPU section and the battery section (cols 17–18 classic, cols 25–26 extended).
- **Fill**: Each column fills upward from row 1; the two columns fill in lock-step with a half-pixel offset so the apparent height interpolates between LED rows.
- **Scale**: Proportional to (1 − MemAvailable / MemTotal) read from `/proc/meminfo`.

#### Battery Status (Left Matrix)
- **Layout**: Block of 7 rows by 13 cols (classic) or 7 rows by 5 cols (extended).
- **Fill**: Cells fill from the right edge leftward across each of the 7 rows; total lit cells is proportional to the battery percentage reported by `/sys/class/power_supply/BAT*/capacity`.
- **Charging overlay**: When `status` reads `Charging`, a lightning-bolt pattern is overlaid at +30 brightness (clamped to 255). Two bolt sizes exist: 7×13 for the classic layout, compact 7×5 for the extended layout.
- **Low-battery safety**: If battery is ≤7% and not charging, the section is drawn empty so the section reads as "critical" rather than as a normal low reading.

#### Disk Activity (Right Matrix)
- **Read**: Bar from the left edge growing rightward, top-section rows 1–3.
- **Write**: Bar from the right edge growing leftward, top-section rows 1–3 (mirror of read).
- **Source**: Sum of read/write sectors for whole-disk devices from `/proc/diskstats` (`sd*`, `vd*`, `hd*`, `xvd*`, `nvme*n*`, `mmcblk*`; partitions excluded).
- **Scale**: Adaptive — tracks the rolling-high observed rate with exponential decay (~70 s half-life) so a one-time burst no longer pegs the scale forever.

#### Network Activity (Right Matrix)
- **Upload**: Bar from the left edge growing rightward, bottom-section rows 5–7.
- **Download**: Bar from the right edge growing leftward, bottom-section rows 5–7.
- **Source**: Sum of TX/RX bytes for all non-loopback interfaces from `/proc/net/dev`.
- **Scale**: Same adaptive decay as disk.

### Visual Encoding

#### Brightness Levels
- **Background** (borders/dividers): defaults `min=12 / max=35`, configurable (0–255)
- **Foreground** (data pixels): defaults `min=24 / max=160`, configurable (0–255)
- **Adaptive**: Each frame, `screen_brightness` (read from `/sys/class/backlight/`) interpolates between the min and max values, so the matrix dims with the laptop display.

#### Update Behavior
- **Cycle**: 100 ms by default (`update_interval_ms`)
- **Frame delivery**: Each frame the main thread builds two `LEDGrid`s and hands them off to per-matrix worker threads via a single-slot mailbox; if a worker is still busy when a new frame arrives, the old pending frame is dropped (newest-wins).
- **Transmit**: Each frame writes 9 `StageCol` rows plus one `FlushCols` over USB CDC-ACM at 115200 baud.

#### Border Elements
- **Left Matrix**: Decorative borders around sections
- **Right Matrix**: Column separators for clarity
- **Purpose**: Visual organization and aesthetic appeal

## Architecture

The daemon uses a multi-threaded design:
- **Main thread**: Reads `/proc` and `/sys`, builds the two `LEDGrid`s, sleeps for `update_interval_ms`, repeats.
- **Drawing threads** (2): One per LED matrix. Each owns its serial fd, opens it lazily on first frame, and re-opens after communication failure.
- **Hand-off**: A single-slot mailbox per worker, guarded by a mutex and condvar. If the worker is still draining the previous frame when a new one arrives, the new frame overwrites the pending one (newest-wins drop policy) and the dropped frame is counted in stats.
- **Shutdown**: SIGTERM/SIGINT set a flag; the main loop exits and `cleanup_drawing_thread()` clears each matrix and joins the worker.

## Configuration

The daemon reads a plain text `key = value` file (one setting per line; `#` starts a comment; unrecognized keys are silently ignored).

### Configuration File Locations (searched in order)
1. `/etc/led_monitor.conf` (system-wide)
2. `~/.config/led_monitor.conf` (user-specific — only consulted if the system file is missing)
3. Command-line specified config (`--config FILE`)

### Configuration Options

Recognized keys (everything else is ignored):

```
# Device USB-path fragments (matched against /dev/serial/by-path/*)
left_device  = 4.2
right_device = 3.3

# Display brightness range (0–255); actual value scales with laptop backlight
min_background_brightness = 12
max_background_brightness = 35
min_foreground_brightness = 24
max_foreground_brightness = 160

# Main-loop tick in milliseconds (default 100 = 10 Hz)
update_interval_ms = 100

# Logging
log_level            = info    # debug | info | warn | error
enable_debug_logging = false   # true | false | 1 | 0
```

### Runtime Configuration Reload
```bash
# Send reload signal to running daemon
sudo systemctl reload led-monitor
```

## File Structure

### Core Files
- `ledmonitord.c` - Main daemon implementation
- `system_monitor.c` - System metric collection functions
- `led_drawing.c` - LED visualization algorithms
- `serial_comm.c` - Framework module communication protocol
- `led_monitor.h` - Shared structures and function declarations
- `led_config.h/c` - Configuration file management
- `led_logging.h/c` - Enhanced logging system
- `led_errors.h/c` - Error handling and codes
- `led_stats.h/c` - Statistics and monitoring

### System Integration
- `led-monitor.service` - Systemd service configuration
- `led-monitor.timer` - Systemd timer for health monitoring
- `Makefile` - Build configuration

## Performance Comparison

| Feature | C Daemon (ledmonitord) | Python Version |
|---------|------------------------|----------------|
| Memory Usage | ~3-6 MB | ~30-50 MB |
| CPU Usage | <1% | 2-5% |
| Startup Time | <200ms | ~2s |
| Dependencies | libc + pthread + libm | Python, numpy, psutil, pyserial |
| Configuration | Flat `key = value` file | Python config |
| Runtime Reload | Signal-based (SIGHUP/SIGUSR1) | No |
| Logging | syslog with level filtering | Python `logging` |
| Maintainability | Lower (C) | Higher (Python) |

## Troubleshooting

### Permission Issues
```bash
# Check which group owns the serial device
make check-serial

# Add user to the group (typically dialout)
sudo usermod -a -G dialout $USER
# Log out and back in for changes to take effect

# Verify group membership
groups
```

### Device Not Found
```bash
# Test device detection
./ledmonitord --test-devices

# Check if modules are detected
ls -la /dev/serial/by-path/*Framework*

# Check USB devices
lsusb | grep "32ac"
```

### Service Not Starting
```bash
# Check service logs
sudo journalctl -u led-monitor -n 50

# Run daemon in foreground to see errors
sudo ./ledmonitord --foreground --debug

# Check configuration file syntax
./ledmonitord --config /etc/led_monitor.conf --test-devices
```

### Configuration Issues
```bash
# Reload configuration without restart
sudo systemctl reload led-monitor

# Show effective log level (the daemon logs this each time set_log_level runs)
sudo journalctl -u led-monitor -f | grep "Log level"
```

Note: unrecognized keys in the config file are silently ignored. To verify a setting is being applied, start the daemon in foreground with `--debug`:

```bash
sudo /usr/local/bin/ledmonitord --foreground --debug --config /etc/led_monitor.conf
```

### Frame / Device Diagnostics
The daemon writes detailed log lines only with `--verbose` or `--debug`:

```bash
# Per-frame pixel count (every 50 frames, verbose mode)
sudo journalctl -u led-monitor | grep "Drawing grid"

# Device reconnection events
sudo journalctl -u led-monitor | grep "reconnected"
```

## License

Same as the original Python implementation.

## Attribution

This C implementation is based on the original Python version by Jeremy Karstrom:
- **Original Project**: https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git
- **Author**: Jeremy Karst
- **License**: Same license as original implementation


The core visualization algorithms, LED matrix layout, and system monitoring concepts are derived from the original Python codebase, with significant performance optimizations and enterprise features added in this C implementation.

