CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread -lm

TARGET = ledmonitord
SOURCES = ledmonitord.c system_monitor.c led_drawing.c serial_comm.c led_config.c led_logging.c led_errors.c led_stats.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = led_monitor.h led_config.h led_logging.h led_errors.h led_stats.h 

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SYSTEMD_DIR = /etc/systemd/system

.PHONY: all clean install uninstall service check-serial

all: $(TARGET) check-serial

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

check-serial:
	@echo ""
	@echo "========================================="
	@echo "Checking serial device permissions..."
	@if [ -e /dev/ttyACM0 ]; then \
		GROUP=$$(stat -c %G /dev/ttyACM0 2>/dev/null || stat -f %Sg /dev/ttyACM0 2>/dev/null); \
		echo "Found /dev/ttyACM0 owned by group: $$GROUP"; \
		echo ""; \
		echo "To access the LED modules, the user running the daemon"; \
		echo "must be a member of the '$$GROUP' group."; \
		echo ""; \
		echo "Add your user to the group with:"; \
		echo "  sudo usermod -a -G $$GROUP $$USER"; \
		echo "Then logout and login again for changes to take effect."; \
		echo ""; \
		echo "Current user groups: $$(id -nG)"; \
		if id -nG | grep -q "$$GROUP"; then \
			echo "Current user is in the $$GROUP group"; \
		else \
			echo "Current user is NOT in the $$GROUP group"; \
		fi; \
	else \
		echo "Note: /dev/ttyACM0 not found."; \
		echo "LED modules may not be connected or may use a different device."; \
		echo "Check /dev/serial/by-id/ for Framework devices."; \
	fi
	@echo "========================================="
	@echo ""

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

led_monitor_daemon.o: led_monitor_daemon.c
	$(CC) $(CFLAGS) -c $< -o $@

system_monitor.o: system_monitor.c
	$(CC) $(CFLAGS) -c $< -o $@

led_drawing.o: led_drawing.c
	$(CC) $(CFLAGS) -c $< -o $@

serial_comm.o: serial_comm.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	@echo "Installing daemon to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "Installation complete."
	@echo "To install as a systemd service, run: sudo make service"

uninstall:
	@echo "Stopping service if running..."
	-systemctl stop led-monitor.service 2>/dev/null
	-systemctl disable led-monitor.service 2>/dev/null
	@echo "Removing daemon..."
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(SYSTEMD_DIR)/led-monitor.service
	@echo "Uninstallation complete."

service: install
	@echo "Creating framework service account..."
	-useradd --system --no-create-home --gid dialout framework 2>/dev/null || true
	@echo "Installing systemd service..."
	install -m 644 led-monitor.service $(SYSTEMD_DIR)/
	systemctl daemon-reload
	systemctl enable led-monitor.service
	@echo "Service installed and enabled."
	@echo "Start it with: sudo systemctl start led-monitor"

debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET) --foreground
