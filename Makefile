# ==============================================================================
# MyOS Makefile - Educational x86-64 Operating System
# ==============================================================================

# Cross-compiler target prefix.
# Standard cross-compiler targets:
#   x86_64-elf-gcc       (Standard OSDev cross-compiler - recommended)
#   x86_64-linux-gnu-gcc (Default on Ubuntu/Debian/WSL)
TARGET ?= x86_64-linux-gnu

CC      := $(TARGET)-gcc
LD      := $(TARGET)-ld
AS      := $(TARGET)-as
QEMU    ?= qemu-system-x86_64

# Project Directory Layout
SRC_DIR   := src
BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/isodir

# Output Binaries
KERNEL_BIN := $(BUILD_DIR)/myos.bin
KERNEL_ISO := $(BUILD_DIR)/myos.iso

# C Compiler Flags
# -std=c99: Modern C standard
# -ffreestanding: Freestanding environment (no host standard library, no main() requirement)
# -mno-red-zone: Crucial for x86-64 bare metal! Disables 128-byte red zone below RSP
# -mno-mmx -mno-sse -mno-sse2: Prevents SIMD instructions before FPU/SSE is configured
# -fno-pie -no-pie: Disables Position-Independent Executable generation
# -mcmodel=small: Kernel linked in lower memory addresses (< 2 GiB)
# -Wall -Wextra: Enable strict compiler diagnostics
CFLAGS := -std=c99 -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -fno-pie -no-pie -mcmodel=small -Wall -Wextra -O2 -I$(SRC_DIR)/kernel

# Linker Flags
# -nostdlib: Do not link standard C runtime startup or libraries
# -z max-page-size=0x1000: Set ELF page size alignment to 4 KiB
# -T linker.ld: Use custom linker script
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker.ld

# Assembly Flags (using GCC frontend for preprocessing and assembling)
ASFLAGS := -c

# Sources and Object Files
C_SRCS   := $(SRC_DIR)/kernel/kernel.c $(SRC_DIR)/kernel/vga.c $(SRC_DIR)/kernel/idt.c $(SRC_DIR)/kernel/pic.c $(SRC_DIR)/kernel/keyboard.c $(SRC_DIR)/kernel/terminal.c $(SRC_DIR)/kernel/shell.c $(SRC_DIR)/kernel/timer.c
ASM_SRCS := $(SRC_DIR)/arch/x86_64/boot.S $(SRC_DIR)/arch/x86_64/interrupts.S

C_OBJS   := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(ASM_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

# ------------------------------------------------------------------------------
# Build Targets
# ------------------------------------------------------------------------------
.PHONY: all clean run run-iso iso check-env help

all: $(KERNEL_BIN)

# Link the kernel ELF binary
$(KERNEL_BIN): $(OBJS) linker.ld
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "[SUCCESS] Kernel binary created at $@"

# Assemble assembly files (.S)
$(BUILD_DIR)/arch/x86_64/%.o: $(SRC_DIR)/arch/x86_64/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $< -o $@

# Compile C files (.c)
$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Create a bootable ISO with GRUB
iso: $(KERNEL_ISO)

$(KERNEL_ISO): $(KERNEL_BIN) grub.cfg
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/myos.bin
	@cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR) 2>/dev/null || \
	  xorriso -as mkisofs -R -r -J -b boot/grub/i386-pc/eltorito.img \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    -o $(KERNEL_ISO) $(ISO_DIR)
	@echo "[SUCCESS] Bootable ISO created at $@"

# Run directly in QEMU via direct kernel boot (-kernel)
run: $(KERNEL_BIN)
	$(QEMU) -kernel $(KERNEL_BIN)

# Run ISO image in QEMU (-cdrom)
run-iso: $(KERNEL_ISO)
	$(QEMU) -cdrom $(KERNEL_ISO)

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@echo "[CLEAN] Removed $(BUILD_DIR) directory."

# Diagnostic check for required host tools
check-env:
	@echo "Checking toolchain components..."
	@which $(CC) >/dev/null 2>&1 && echo "[OK] Compiler: $(CC)" || echo "[FAIL] Missing compiler: $(CC)"
	@which $(LD) >/dev/null 2>&1 && echo "[OK] Linker: $(LD)" || echo "[FAIL] Missing linker: $(LD)"
	@which $(QEMU) >/dev/null 2>&1 && echo "[OK] Emulator: $(QEMU)" || echo "[FAIL] Missing emulator: $(QEMU)"
	@which grub-mkrescue >/dev/null 2>&1 && echo "[OK] ISO Tool: grub-mkrescue" || echo "[WARN] grub-mkrescue not found (needed only for 'make iso')"

help:
	@echo "MyOS Build System Commands:"
	@echo "  make              - Build kernel ELF binary ($(KERNEL_BIN))"
	@echo "  make run          - Boot kernel directly in QEMU using multiboot"
	@echo "  make iso          - Create bootable GRUB ISO ($(KERNEL_ISO))"
	@echo "  make run-iso      - Boot GRUB ISO in QEMU"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make check-env    - Verify toolchain installation"
