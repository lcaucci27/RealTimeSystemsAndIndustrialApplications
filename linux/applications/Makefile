# Makefile for APU Applications
# Cross-compiles for ARM64 (Cortex-A53)

# Toolchain setup
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

# Compiler flags
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = -static
LIBS = -lrt

# What we're building
TARGETS = apu_perf_test apu_coherency_test

# Source files
SOURCES = $(TARGETS:=.c)

# Build everything by default
all: $(TARGETS)

# Generic rule for building executables
%: %.c
	@echo "Building $@..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	@echo "Stripping $@..."
	$(STRIP) $@
	@echo "Done: $@"

# Explicit rules for each target
apu_perf_test: apu_perf_test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	$(STRIP) $@

apu_coherency_test: apu_coherency_test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	$(STRIP) $@

# Clean up build artifacts
clean:
	@echo "Cleaning..."
	rm -f $(TARGETS) *.o
	@echo "Clean done."

# Copy to target board via SCP
install: all
	@echo "Installing to target..."
	@if [ -z "$(BOARD_IP)" ]; then \
		echo "ERROR: BOARD_IP not set. Usage: make install BOARD_IP=192.168.1.100"; \
		exit 1; \
	fi
	@echo "Copying to $(BOARD_USER)@$(BOARD_IP)..."
	@for target in $(TARGETS); do \
		scp $$target $(BOARD_USER)@$(BOARD_IP):/home/root/ && \
		ssh $(BOARD_USER)@$(BOARD_IP) "chmod +x /home/root/$$target" && \
		echo "Installed $$target"; \
	done
	@echo "Installation complete!"

# Help message
help:
	@echo "Makefile for APU Applications"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all applications (default)"
	@echo "  clean            - Remove built files"
	@echo "  install          - Copy to target board (requires BOARD_IP)"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Individual targets:"
	@echo "  apu_perf_test    - Performance measurement application"
	@echo "  apu_coherency_test - Simple coherence test"
	@echo ""
	@echo "Variables:"
	@echo "  CROSS_COMPILE    - Toolchain prefix (default: aarch64-linux-gnu-)"
	@echo "  BOARD_IP         - Target board IP for install"
	@echo "  BOARD_USER       - SSH user (default: root)"
	@echo ""
	@echo "Examples:"
	@echo "  make                                    # Build all"
	@echo "  make apu_perf_test                      # Build specific target"
	@echo "  make install BOARD_IP=192.168.1.100    # Build and install"
	@echo "  make clean                              # Clean build files"

.PHONY: all clean install help