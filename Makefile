CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread -lm

TARGET = ledmonitord
SOURCES = ledmonitord.c system_monitor.c led_drawing.c serial_comm.c led_config.c led_logging.c led_errors.c led_pidfile.c led_stats.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = led_monitor.h led_config.h led_logging.h led_errors.h led_pidfile.h led_stats.h 

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SYSTEMD_DIR = /etc/systemd/system

.PHONY: all clean install uninstall service

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

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
	-systemctl stop led-monitor-daemon.service 2>/dev/null
	-systemctl disable led-monitor-daemon.service 2>/dev/null
	@echo "Removing daemon..."
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(SYSTEMD_DIR)/led-monitor-daemon.service
	@echo "Uninstallation complete."

service: install
	@echo "Installing systemd service..."
	install -m 644 led-monitor-daemon.service $(SYSTEMD_DIR)/
	systemctl daemon-reload
	systemctl enable led-monitor-daemon.service
	@echo "Service installed and enabled."
	@echo "Start it with: sudo systemctl start led-monitor-daemon"

debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET) --foreground