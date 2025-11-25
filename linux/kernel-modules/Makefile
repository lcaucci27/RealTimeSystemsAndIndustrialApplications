# Makefile for coherency_test kernel module

obj-m += coherency_test.o

# Kernel source directory - change this if your kernel sources are elsewhere
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

# Cross-compilation setup for ARM64
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

help:
	@echo "Targets:"
	@echo "  all   - Build kernel module"
	@echo "  clean - Remove built files"
	@echo ""
	@echo "Variables:"
	@echo "  KERNEL_SRC    - Path to kernel source (current: $(KERNEL_SRC))"
	@echo "  ARCH          - Target architecture (current: $(ARCH))"
	@echo "  CROSS_COMPILE - Cross compiler prefix (current: $(CROSS_COMPILE))"