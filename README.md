# MyOS: Stage 1 (Minimal Bare-Metal x86-64 Kernel)

`MyOS` is a small educational x86-64 bare-metal operating system built from scratch.

In **Stage 1**, the sole objective is to establish the minimal project structure, assembly boot stub, 64-bit Long Mode initialization, linker script, and minimal C kernel to boot in QEMU and print a status message via the VGA text buffer (`0xB8000`).

---

## 1. Directory Structure

```text
MyOS/
├── Makefile                     # Cross-compilation build system
├── linker.ld                    # Linker script (places kernel at 1 MiB physical)
├── grub.cfg                     # GRUB2 configuration for bootable ISO creation
├── README.md                    # Project documentation, build guide, and boot flow
└── src/
    ├── arch/
    │   └── x86_64/
    │       └── boot.S           # Assembly entry point, Multiboot headers & 64-bit transition
    └── kernel/
        ├── vga.h                # Minimal VGA text-mode driver header (80x25 buffer)
        ├── vga.c                # VGA driver implementation (puts, putc, clear, scroll)
        └── kernel.c             # C entry point (kernel_main)
```

---

## 2. File Explanations

| File | Purpose |
| :--- | :--- |
| **`Makefile`** | Orchestrates assembling, compiling, linking, ISO generation, and running QEMU. Configured with configurable variables (`TARGET`, `CC`, `LD`, `QEMU`) to make cross-compilation seamless across different host operating systems. |
| **`linker.ld`** | Defines the physical and virtual memory layout for the kernel ELF64 binary. Instructs the linker to load the kernel at 1 MiB (`0x100000`) physical address, place the `.multiboot` header in the first 8 KiB, page-align all standard sections (`.text`, `.rodata`, `.data`, `.bss`), and discard unnecessary debug metadata. |
| **`grub.cfg`** | Directs GRUB to load `/boot/myos.bin` using the Multiboot2 boot protocol with a 0-second boot menu timeout. |
| **`src/arch/x86_64/boot.S`** | The bare-metal entry point (`_start`). Contains Multiboot 1 & Multiboot 2 magic headers, checks CPUID and Long Mode capability, establishes identity-mapped 4-level page tables (PML4, PDPT, PD) for the first 1 GiB, enables PAE and Long Mode in CPU control registers (CR4, EFER, CR0), loads the 64-bit Global Descriptor Table (GDT), executes a far jump into 64-bit code, sets up the 16 KiB kernel stack, and calls `kernel_main()`. |
| **`src/kernel/vga.h`** | Header defining the standard 16 IBM VGA colors, text-mode dimensions (80x25), entry formatting helpers, and public API declarations for the VGA text driver. |
| **`src/kernel/vga.c`** | Minimal self-contained driver interacting with physical video memory at `0xB8000`. Implements character rendering, color formatting, newline (`\n`), carriage return (`\r`), tab expansion, screen clearing, and upward terminal scrolling. |
| **`src/kernel/kernel.c`** | The C kernel entry point (`kernel_main`). Initializes the VGA console, displays a colored banner with boot verification checkpoints confirming long mode and paging, and enters a safe CPU halt loop (`hlt`). |

---

## 3. Complete Boot Flow

From the instant QEMU boots to the execution of `kernel_main()`, the system transitions through the following stages:

```
+-------------------------------------------------------------+
| 1. QEMU Power-On & Firmware Initialization (BIOS / UEFI)   |
|    - CPU begins in 16-bit Real Mode at 0xFFFFFFF0           |
|    - Executes firmware POST and initializes hardware        |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 2. Bootloader Phase (GRUB or QEMU -kernel)                  |
|    - Bootloader scans the first 8 KiB of our ELF binary     |
|    - Locates Multiboot magic headers in .multiboot section  |
|    - Loads ELF program segments into RAM at 1 MiB (0x100000)|
|    - Switches CPU from 16-bit Real Mode to 32-bit Protected|
|    - Passes magic in EAX, multiboot info pointer in EBX     |
|    - Jumps to entry symbol _start (in boot.S)               |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 3. Assembly Entry (_start in boot.S - 32-bit Mode)          |
|    - Interrupts disabled (cli)                              |
|    - Temporary 32-bit stack initialized                     |
|    - CPUID verified (EFLAGS bit 21 toggling test)           |
|    - x86-64 Long Mode capability verified via CPUID         |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 4. 4-Level Paging Initialization                            |
|    - Identity-maps first 1 GiB (0x0 to 0x3FFFFFFF) using    |
|      512 x 2 MiB huge pages: PML4[0] -> PDPT[0] -> PD[0..511]|
|    - CR3 loaded with physical address of PML4               |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 5. Long Mode Activation                                     |
|    - CR4.PAE (bit 5) enabled                                |
|    - IA32_EFER.LME (bit 8) enabled via RDMSR / WRMSR        |
|    - CR0.PG (bit 31) and CR0.PE (bit 0) enabled             |
|    - 64-bit GDT (gdt64) loaded via LGDT                     |
|    - Far jump (ljmp $0x08, $long_mode_start) to 64-bit mode |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 6. 64-bit Long Mode Entry (long_mode_start in boot.S)       |
|    - Segment registers (DS, ES, FS, GS, SS) loaded with 0x10|
|    - 64-bit RSP set to stack_top (16-byte aligned per ABI)  |
|    - Direction flag cleared, interrupts kept disabled       |
|    - Executes: call kernel_main                             |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| 7. C Kernel Execution (kernel_main in kernel.c)             |
|    - vga_init() clears 80x25 screen at 0xB8000              |
|    - Prints colored welcome banner & verification messages  |
|    - Enters safe idle loop: while(1) { __asm__("hlt"); }    |
+-------------------------------------------------------------+
```

---

## 4. Dependencies & Toolchain Requirements

To build and run an x86-64 ELF bare-metal operating system, you require a toolchain capable of generating **x86-64 ELF executables without host standard library dependencies** (`-ffreestanding`).

### Required Tools
1. **Cross-Compiler**: `x86_64-elf-gcc` (recommended) or `x86_64-linux-gnu-gcc`
2. **Linker**: `x86_64-elf-ld` or `x86_64-linux-gnu-ld`
3. **Emulator**: `qemu-system-x86_64`
4. **Build Utility**: `make`
5. **ISO Tools (Optional, for `make iso`)**: `grub-mkrescue` and `xorriso`

---

## 5. How to Build and Run

### On Linux / WSL (Ubuntu / Debian):
```bash
# Install required packages
sudo apt update
sudo apt install build-essential gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu qemu-system-x86 xorriso grub-pc-bin grub-common

# Build the kernel ELF binary
make TARGET=x86_64-linux-gnu

# Boot directly in QEMU (Multiboot)
make TARGET=x86_64-linux-gnu run

# Alternatively, create and boot a bootable GRUB ISO:
make TARGET=x86_64-linux-gnu iso
make TARGET=x86_64-linux-gnu run-iso
```

### With a standard `x86_64-elf` cross-compiler:
```bash
# Build
make

# Run in QEMU
make run
```
