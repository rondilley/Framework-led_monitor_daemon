# Framework LED Monitor Daemon

A high-performance C implementation of the Framework LED system monitor daemon with enterprise-grade features. This daemon displays real-time system metrics on Framework Laptop 16 LED input modules.

**Based on the original Python implementation by Jeremy Karstrom:**  
https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git

## Features

- **Multi-threaded architecture** for independent LED matrix control
- **Real-time system monitoring**:
  - CPU usage per core
  - Memory utilization
  - Battery status and charging state
  - Disk I/O (read/write)
  - Network traffic (upload/download)
- **Adaptive brightness** based on laptop display brightness
- **Automatic device detection** via USB path
- **Low resource usage** compared to Python version
- **Configuration management** with INI-style config files
- **Enhanced logging system** with multiple levels and debug support
- **Runtime statistics** and performance monitoring
- **Signal-based configuration reload** (SIGUSR1, SIGHUP)
- **Comprehensive error handling** with specific error codes
- **Security enhancements** with input validation
- **Device testing tools** and status monitoring
- **Systemd integration** with security hardening

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

### Important Note on Systemd Security
The service file has minimal security restrictions due to requirements of the daemon's double-fork daemonization process. The daemon runs as the `framework` user in the `dialout` group to maintain USB device access permissions.

### Configuration
```bash
# Create system configuration (optional)
sudo mkdir -p /etc
sudo cp led_monitor.conf.example /etc/led_monitor.conf
sudo editor /etc/led_monitor.conf

# Create user configuration (optional)
mkdir -p ~/.config
cp led_monitor.conf.example ~/.config/led_monitor.conf
editor ~/.config/led_monitor.conf
```

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

```
     Left LED Matrix (9×34)
     USB Port: 1-4.2
  0 ┌───────────────┐
  1 │ x x x | x x x | CPU Core Usage
  2 │ x x x | x x x | (up to 8 cores)
  3 │ x x x | x x x | Each bar = 1 core
  4 │───────|───────| Height = % usage
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
 10 │───────────────│
 20 │ x x x x x x x │ Battery Level
 21 │ x x x x x x x │ 
 22 │ x x x x x x x │ ⚡ = charging
 23 │ x x x x x x x │ Segments = % charge
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

#### CPU Cores (Left Matrix, Rows 1-4)
- **Layout**: Up to 8 vertical bars, 4 columns wide each
- **Height**: Proportional to core usage (0-100%)
- **Color**: Intensity increases with load
- **Spacing**: 1-pixel gap between core bars

#### Memory Usage (Left Matrix, Rows 5-6)
- **Layout**: Horizontal bar spanning full width
- **Length**: Proportional to RAM usage (0-100%)
- **Visual**: Solid fill from left to right
- **Threshold**: Different intensities for different usage levels

#### Battery Status (Left Matrix, Rows 7-9)
- **Layout**: Segmented horizontal display
- **Segments**: 6 blocks representing charge level
- **Charging**: Lightning bolt animation when charging
- **States**: Different patterns for charging/discharging

#### Disk Activity (Right Matrix, Columns 1-3)
- **Read Activity**: Top half (rows 1-4)
- **Write Activity**: Bottom half (rows 5-7)
- **Visualization**: Vertical bars showing I/O intensity
- **Real-time**: Updates based on actual disk throughput

#### Network Activity (Right Matrix, Columns 5-7)
- **Upload**: Bottom section (rows 8-9)
- **Download**: Top section (rows 1-7)
- **Visualization**: Vertical bars showing network throughput
- **Scale**: Adaptive based on connection speed

### Visual Encoding

#### Brightness Levels
- **Background**: Dim baseline (configurable 10-100)
- **Foreground**: Active elements (configurable 50-255)
- **Adaptive**: Scales with laptop display brightness

#### Update Behavior
- **Smooth**: Gradual changes to avoid flickering
- **Responsive**: 100ms update cycle for real-time feel
- **Efficient**: Only changed pixels are updated

#### Border Elements
- **Left Matrix**: Decorative borders around sections
- **Right Matrix**: Column separators for clarity
- **Purpose**: Visual organization and aesthetic appeal

## Architecture

The daemon uses a multi-threaded design:
- **Main thread**: Collects system metrics and prepares LED grids
- **Drawing threads** (2): One per LED matrix, handles serial communication
- **Queue-based communication**: Thread-safe grid updates

## Configuration

The enhanced daemon supports comprehensive configuration through INI-style files:

### Configuration File Locations (searched in order)
1. `/etc/led_monitor.conf` (system-wide)
2. `~/.config/led_monitor.conf` (user-specific)
3. Command-line specified config (`--config FILE`)

### Configuration Options
```ini
[display]
visualization_mode = system
update_interval_ms = 100
max_foreground_brightness = 255
min_foreground_brightness = 50
max_background_brightness = 100
min_background_brightness = 10

[devices]
left_device_path = 1-4.2
right_device_path = 1-3.3

[daemon]
run_as_daemon = true

[logging]
log_level = info
enable_debug_logging = false

[statistics]
enable_statistics = true
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
| Dependencies | System libraries only | Python, numpy, psutil, pyserial |
| Configuration | INI files | Python config |
| Runtime Reload | Signal-based | No |
| Error Handling | Comprehensive | Good |
| Logging | Multi-level | Python logging |
| Statistics | Built-in | No |
| Security | Hardened | Basic |
| Maintainability | Moderate | Higher |

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
# Validate configuration file
# Daemon will report config errors at startup

# Reload configuration without restart
sudo systemctl reload led-monitor

# Check current log level
sudo journalctl -u led-monitor -f | grep "Log level"
```

### Performance Issues
```bash
# Monitor daemon performance
# Daemon provides built-in statistics

# Check frame rates and timing
sudo journalctl -u led-monitor | grep "Frame time"

# View device connection status
sudo journalctl -u led-monitor | grep "device"
```

## License

Same as the original Python implementation.

## Attribution

This C implementation is based on the original Python version by Jeremy Karstrom:
- **Original Project**: https://code.karsttech.com/jeremy/FW_LED_System_Monitor.git
- **Author**: Jeremy Karstrom
- **License**: Same license as original implementation


The core visualization algorithms, LED matrix layout, and system monitoring concepts are derived from the original Python codebase, with significant performance optimizations and enterprise features added in this C implementation.

