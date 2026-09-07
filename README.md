# MyOS: Educational Bare-Metal x86-64 Operating System

`MyOS` is a small educational x86-64 bare-metal operating system built from scratch in freestanding C and x86-64 assembly.

---

## 1. Project Stages & Capabilities

- **Stage 1: Bootable x86-64 Kernel**
  - Multiboot 1 & Multiboot 2 compliance.
  - 32-bit Protected Mode entry transitioning to 64-bit Long Mode.
  - 4-level identity paging mapping the first 1 GiB using 2 MiB huge pages.
  - 64-bit GDT and 16 KiB kernel stack.
  - 80x25 VGA text-mode driver at `0xB8000`.

- **Stage 2A: Interrupt Foundation (IDT & PIC)**
  - 256-entry Interrupt Descriptor Table (IDT) with 16-byte 64-bit gates.
  - Dual 8259 PIC remapped to vectors `0x20..0x2F` out of CPU exception range.
  - Assembly ISR stubs preserving 15 general-purpose registers and honoring 16-byte stack alignment.

- **Stage 2B: PS/2 Keyboard Input (IRQ1)**
  - PS/2 controller driver handling make/break scancodes (Set 1).
  - Shift and Caps Lock modifier tracking with ASCII translation.
  - IRQ1 connected to IDT vector `0x21`.

- **Stage 3A: Terminal Line Discipline**
  - Clean separation: Keyboard Driver → Terminal → VGA.
  - 256-byte static line buffer with prompt protection on Backspace.
  - Interactive prompt `MyOS> `.

- **Stage 3B: Command-Line Shell & Built-ins**
  - Command table architecture with freestanding string helpers (`kstrcmp`, `kstrlen`).
  - Fixed-size token parser preventing buffer overflows.
  - Built-in commands: `help`, `clear`, `about`, `echo`, `halt`.

- **Stage 4: Hardware Timer & Timekeeping**
  - Legacy 8253/8254 Programmable Interval Timer (PIT) Channel 0 configured in Mode 3 (Square Wave).
  - Configured for 100 Hz target frequency (divisor 11931 from base frequency 1,193,182 Hz).
  - IRQ0 mapped to IDT vector `0x20` with low-level assembly ISR `isr_timer`.
  - Monotonic 64-bit tick counter (`timer_get_ticks()`) and uptime API (`timer_get_uptime_ms()`).
  - Freestanding 64-bit decimal integer printer (`vga_print_dec()`).
  - Shell command `uptime` reporting elapsed seconds and raw tick count.

---

## 2. Directory Structure

```text
MyOS/
├── Makefile                     # Cross-compilation build system
├── linker.ld                    # Linker script (kernel loaded at 1 MiB physical)
├── grub.cfg                     # GRUB2 configuration for bootable ISO creation
├── README.md                    # Project documentation
├── test_keyboard.py             # Stage 2B automated test suite
├── test_stage3a.py              # Stage 3A automated test suite
├── test_stage3b.py              # Stage 3B automated test suite
├── test_stage4.py               # Stage 4 automated test suite
└── src/
    ├── arch/
    │   └── x86_64/
    │       ├── boot.S           # Multiboot headers & 32-bit to 64-bit transition
    │       └── interrupts.S     # Low-level 64-bit ISR stubs (timer, keyboard)
    └── kernel/
        ├── io.h                 # Port I/O inline assembly (inb, outb, io_wait)
        ├── vga.h                # VGA text mode interface (colors, print_dec, puts)
        ├── vga.c                # VGA driver implementation
        ├── idt.h                # IDT descriptor structures & vector definitions
        ├── idt.c                # IDT table & lidt loading
        ├── pic.h                # 8259 PIC interface & EOI handling
        ├── pic.c                # PIC initialization, remapping & masking
        ├── keyboard.h           # PS/2 keyboard interface
        ├── keyboard.c           # Scancode translation & IRQ1 handler
        ├── terminal.h           # Terminal line discipline interface
        ├── terminal.c           # Terminal buffer management & prompt protection
        ├── shell.h              # Command parser interface
        ├── shell.c              # Shell dispatch table & built-in commands
        ├── timer.h              # PIT hardware timer interface & constants
        ├── timer.c              # PIT channel 0 driver & 64-bit tick counter
        └── kernel.c             # C entry point (kernel_main)
```

---

## 3. Dependencies & Toolchain Requirements

- **Cross-Compiler**: `x86_64-linux-gnu-gcc` (Ubuntu/WSL) or `x86_64-elf-gcc`
- **Linker**: `x86_64-linux-gnu-ld` or `x86_64-elf-ld`
- **Assembler**: GNU `as`
- **Emulator**: `qemu-system-x86_64`
- **Build Utility**: GNU `make`
- **Test Runner**: Python 3

---

## 4. How to Build, Test, and Run

### Build Kernel Binary:
```bash
make clean && make
```

### Run in QEMU:
```bash
make run
```

### Run Automated Tests:
```bash
python3 test_stage4.py
```
