# Stage 12A Walkthrough: ATA PIO Primary Master Disk Driver

## 1. Executive Summary

Stage 12A introduces persistent disk controller support to MyOS through a polling-based ATA Programmed I/O (PIO) driver for the Primary Master channel:

1. **16-bit Port I/O Primitives**:
   - Implemented `inw(uint16_t port)` and `outw(uint16_t port, uint16_t val)` in `src/kernel/io.h` using GCC inline assembly.
   - Bound explicitly to `%ax` register with volatile semantics to prevent compiler reordering.
2. **ATA PIO Driver (`src/kernel/ata.h`, `src/kernel/ata.c`)**:
   - Hardware register definitions for Primary Bus (`0x1F0`–`0x1F7`) and Device Control / Alternate Status (`0x3F6`).
   - Drive identification (`IDENTIFY`, 0xEC) with floating bus (`0xFF`/`0x00`) and ATAPI/SATA signature checks.
   - 28-bit LBA single-sector reading (`READ SECTORS`, 0x20) and writing (`WRITE SECTORS`, 0x30).
   - Alternate Status (`0x3F6`) polling to prevent clearing controller interrupt requests.
   - Write completion synchronization waiting for `BSY == 0` followed by `FLUSH CACHE` (`0xE7`).
   - Bounded timeout loops (2,000,000 iterations) preventing system hangs.
   - Model string byte swapping and space trimming.
3. **Shell Diagnostics (`src/kernel/shell.c`)**:
   - `diskinfo`: Displays Primary Master geometry (Model, Sector Size, LBA28 Sector Count, Capacity in MB).
   - `disktest`: Non-destructive verification sequence on reserved sector LBA 8 (read original, write multi-pattern test data, verify, restore, verify restoration).
   - Negative parameter validation (rejects NULL pointers, LBA overflow, capacity limits).
   - Updated `about` and `help` commands within screen budget constraints.
4. **Build System & Environment (`Makefile`)**:
   - Added raw 32 MiB disk image generation target `$(BUILD_DIR)/disk.img` via `dd`.
   - Attached disk image to QEMU via `-hda $(BUILD_DIR)/disk.img`.
5. **Decoupled Architecture**:
   - Zero VFS integration: RAMFS remains the sole mounted root filesystem (`/`).
   - Purely polling-based: IRQ14/IRQ15 remain masked; no interrupts used.

---

## 2. Layering Architecture

```text
Shell Commands (diskinfo, disktest)
      │
      ▼
ATA PIO Driver Interface (ata.h, ata.c)
  ├── ata_init()           - Silent probe during kernel initialization
  ├── ata_identify()       - Probe drive, parse model string, geometry, LBA capacity
  ├── ata_read_sector()    - Read 512 bytes via LBA28 + 256-word inw() loop
  ├── ata_write_sector()   - Write 512 bytes via LBA28 + 256-word outw() loop + FLUSH CACHE
  └── Informational getters (ata_is_present, ata_get_sector_count, etc.)
      │
      ▼
I/O Port Subsystem (io.h)
  ├── inb / outb           - 8-bit port I/O
  └── inw / outw           - 16-bit port I/O (ATA Data Register 0x1F0)
      │
      ▼
Hardware / QEMU IDE Controller
  ├── Base Ports: 0x1F0 - 0x1F7
  └── Control Port: 0x3F6 (Alternate Status)
```

---

## 3. Verification & Quality Gates

### Build Quality
- Toolchain: `x86_64-linux-gnu-gcc -std=c99 -Wall -Wextra -O2`
- **Compiler Warnings**: 0
- **Linker Warnings**: 0
- **Git Whitespace Errors (`git diff --check`)**: 0

### Automated Test Matrix
All 19 test suites passing 100%:
- `test_stage12a.py`: **PASS (8/8 tests)**
- Regression suites (Stages 3A through 11E, and keyboard test): **18/18 PASS**
