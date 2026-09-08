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

- **Stage 5A: Physical Memory Manager / 4 KiB Frame Allocator**
  - Multiboot 1 memory map parser discovering usable physical RAM regions.
  - Compact static frame bitmap in BSS tracking 4 KiB frames up to 1 GiB.
  - Automatic reservation of critical memory (low 1 MiB, IVT/BDA, VGA at `0xB8000`, kernel image boundaries, boot stack, page tables, bitmap, Multiboot structures).
  - Physical frame allocation (`pmm_alloc_frame`) and deallocation (`pmm_free_frame`) with frame 0 reservation.
  - Freestanding 64-bit hexadecimal address printer (`vga_print_hex()`).
  - Shell commands: `meminfo` (memory accounting), `alloc` (frame allocation), `free` (frame deallocation).

- **Stage 5B: Virtual Memory Manager / 4 KiB Page Mapping**
  - 4-level paging management (PML4 -> PDPT -> PD -> PT) using active CR3 root.
  - Preserves existing boot-time 2 MiB identity mapping (0–1 GiB).
  - Dynamic intermediate page-table allocation through PMM with mandatory zero-initialization.
  - Virtual-to-physical 4 KiB page mapping (`vmm_map_page`), unmapping (`vmm_unmap_page`), and query (`vmm_get_mapping`).
  - Individual TLB invalidation via `invlpg` instruction.
  - CPU Exception 14 Page Fault (#PF) diagnostic ISR reading faulting address from CR2 and decoding error code.
  - Shell commands: `vmmap` (virtual memory layout & test region) and `vmtest` (end-to-end PMM -> VMM -> write/read access -> unmap -> free verification).
  - *Note: Stage 5B establishes page mapping infrastructure and does not yet implement user-space memory or processes.*

- **Stage 6: Kernel Heap / Dynamic Memory Allocation**
  - Fixed-size 64 KiB static kernel heap located at virtual address `0x50000000` (1 GiB + 256 MiB).
  - Backed by 16 physical 4 KiB frames allocated via PMM and mapped via VMM with cleanup on partial failure.
  - Singly linked-list free-list allocator using 24-byte aligned metadata headers (`struct heap_block`).
  - First-fit allocation strategy via `kmalloc(size)` with strict 8-byte payload alignment.
  - Block splitting when excess space meets header + minimum payload (8 bytes).
  - Safe deallocation via `kfree(ptr)` with pointer boundary, alignment, and existence validation.
  - Automatic coalescing of adjacent free blocks (prev+curr, curr+next, prev+curr+next).
  - Shell commands: `heapinfo` (heap usage, block counts, and fragmentation metrics) and `heaptest` (12-step in-kernel test suite).

- **Stage 7A: Kernel Task Infrastructure & Manual Context Switching**
  - Task Control Block (TCB) infrastructure managing up to 4 concurrent kernel tasks (`task_t`).
  - Task states: `TASK_UNUSED`, `TASK_READY`, `TASK_RUNNING`, `TASK_FINISHED`.
  - Task 0 (`main`) encapsulates the initial boot execution thread using the pre-allocated kernel stack (`0x10B000`).
  - Dynamic kernel task stack allocation via `kmalloc(4096)` with 16-byte System V ABI alignment.
  - Low-level assembly context switch (`context_switch.S`) preserving callee-saved registers (`%rbx`, `%rbp`, `%r12..%r15`), `RFLAGS` via `pushfq`/`popfq`, and `%rsp`.
  - Task bootstrap trampoline (`task_bootstrap`) executing task entry point and cleanly transitioning to `task_exit()` upon completion.
  - Cooperative context switching demonstration during boot interleaving Task A and Task B execution before returning cleanly to Task 0 (main shell).
  - Automatic slot recycling and stack deallocation on reuse.
  - Shell commands: `tasks` (display task IDs, names, states, and stack bases) and `tasktest` (re-run cooperative context switching demonstration).
  - *Note: Stage 7A establishes cooperative kernel-mode multitasking and does not implement timer preemption, priority scheduling, or user mode (Ring 3).*

- **Stage 7B: Timer-Driven Round-Robin Scheduler**
  - Preemptive kernel multitasking driven by the 8253/8254 PIT hardware timer (IRQ0 / IDT vector `0x20` at 100 Hz).
  - Complete 20-quadword interrupt frame (160 bytes) in `isr_timer` capturing hardware state (`SS`, `RSP`, `RFLAGS`, `CS`, `RIP`) and 15 general-purpose registers (`%r15..%rax`).
  - Preemptive stack switching: `timer_interrupt_handler` receives interrupted `%rsp`, invokes `scheduler_tick`, and returns chosen task's `%rsp` for stack exchange and `iretq` dispatch.
  - Round-robin task selection across active kernel tasks (`TASK_READY` or `TASK_RUNNING`), skipping `TASK_UNUSED` and `TASK_FINISHED` tasks.
  - Re-entrancy guard preventing nested scheduler invocations.
  - Dedicated static task stacks in `.bss` (4 KiB each, 16-byte aligned) preventing heap contamination and preserving heap test invariants (`Used: 0 bytes` at idle).
  - Preemptive task lifecycle: exiting tasks mark `TASK_FINISHED` and halt, allowing the scheduler to deschedule them on the next timer interrupt without cooperative frame corruption.
  - Boot demonstration: 3 concurrent tasks (Task A, Task B, Task C) execute compute loops producing interleaved output `[A] [B] [C] [A] [B] [C]`, with Task C terminating cleanly and Tasks A & B continuing background computation.
  - Shell commands: `tasks` (updated with `Switches` column tracking per-task context switch count) and `sched` (reports scheduler policy, timer frequency, total ticks, context switches, current task PID/name, and active tasks).
  - *Note: Stage 7B establishes preemptive kernel-mode round-robin scheduling and does not implement priority queues, dynamic sleep/blocking, user mode (Ring 3), or SMP.*

- **Stage 8A: User Mode / Ring 3 Foundation**
  - Global Descriptor Table (GDT) expanded to 6 descriptors (7 entries / 56 bytes) containing Kernel Code (`0x08`, DPL 0), Kernel Data (`0x10`, DPL 0), User Data (`0x18`, DPL 3, RPL 3: `0x1B`), User Code (`0x20`, DPL 3, RPL 3: `0x23`), and a 16-byte 64-bit Available TSS descriptor (`0x28` / `0x30`, DPL 0).
  - 64-bit Task State Segment (TSS) defined per AMD64/Intel specifications with dedicated 16-byte aligned kernel interrupt stack (`user_kernel_stack`) configured in `RSP0` and loaded into the CPU Task Register (`ltr 0x28`).
  - User virtual address space at `0x60000000` (code page) and `0x60001000` (stack/data page, stack top `0x60002000`) mapped with `PTE_USER | PTE_WRITABLE | PTE_PRESENT`.
  - VMM updated with `vmm_get_page_flags()` and hierarchical `PTE_USER` propagation across PML4, PDPT, and PD tables, ensuring user access is architecturally permitted while retaining supervisor-only protection on kernel identity pages.
  - Dedicated low-level privilege transition routine `switch_to_user_mode` constructing a 5-quadword `IRETQ` frame (`SS=0x1B`, `RSP=0x60002000`, `RFLAGS=0x202`, `CS=0x23`, `RIP=0x60000000`) and executing `iretq`.
  - Minimal user test program executing entirely at CPL 3: reads `%cs`, confirms `CPL == 3`, writes `observed_cpl = 3` and canary magic `0x1337BEEF` to user memory, runs an iteration loop, and traps cleanly back to Ring 0 via IDT vector `0x80` (`IDT_FLAG_USER_INTERRUPT_GATE = 0xEE`).
  - Privilege transition return routine `isr_user_return` restoring kernel data segments, capturing hardware-saved `CS` (`0x23`) and `SS` (`0x1B`), and returning to C kernel control.
  - Shell commands: `gdtinfo` (displays GDT descriptor table, selectors, and TSS RSP0 state) and `usertest` (triggers Ring 3 transition and validates CPL 3, canary magic, loop count, and hardware frames).
  - *Note: Stage 8A establishes the hardware and memory foundations for Ring 3 and does not yet implement syscall/sysret, ELF loading, file systems, or separate user process address spaces.*

```text
Preemptive Timer-Driven Scheduler Architecture:

   Task A Running (Busy compute)
         |
         | Hardware IRQ0 (100 Hz timer tick)
         v
   +-------------------------------------------------------+
   | isr_timer (src/arch/x86_64/interrupts.S)              |
   | 1. Hardware pushes: SS, RSP, RFLAGS, CS, RIP (40 B)   |
   | 2. Pushes: RAX, RCX, RDX, RBX, RBP, RSI, RDI,         |
   |            R8, R9, R10, R11, R12, R13, R14, R15 (120B)|
   | 3. movq %rsp, %rdi                                    |
   | 4. call timer_interrupt_handler                       |
   +-------------------------------------------------------+
         |
         v
   +-------------------------------------------------------+
   | timer.c: timer_interrupt_handler                      |
   | - timer_ticks++                                       |
   | - pic_send_eoi(0)                                     |
   | - return scheduler_tick(current_rsp)                 |
   +-------------------------------------------------------+
         |
         v
   +-------------------------------------------------------+
   | scheduler.c: scheduler_tick                           |
   | - curr->rsp = current_rsp                             |
   | - curr->state = TASK_READY                            |
   | - Round-Robin select: candidate = (curr->id + 1) % N  |
   | - next->state = TASK_RUNNING                          |
   | - next->switch_count++                                |
   | - context_switches++                                  |
   | - return next->rsp                                    |
   +-------------------------------------------------------+
         |
         v
   +-------------------------------------------------------+
   | isr_timer (src/arch/x86_64/interrupts.S)              |
   | 5. movq %rax, %rsp   <-- Switch stack to Task B       |
   | 6. popq %r15..%rax   <-- Restore Task B registers     |
   | 7. iretq             <-- Resume/dispatch Task B       |
   +-------------------------------------------------------+
         |
         v
   Task B Resumed / Running
```

---

## 2. Directory Structure

```text
MyOS/
├── Makefile                     # Cross-compilation build system
├── linker.ld                    # Linker script (places kernel at 1 MiB physical)
├── grub.cfg                     # GRUB2 configuration for bootable ISO creation
├── README.md                    # Project documentation
├── test_keyboard.py             # Stage 2B automated test suite
├── test_stage3a.py              # Stage 3A automated test suite
├── test_stage3b.py              # Stage 3B automated test suite
├── test_stage4.py               # Stage 4 automated test suite
├── test_stage5a.py              # Stage 5A automated test suite
├── test_stage5b.py              # Stage 5B automated test suite
├── test_stage6.py               # Stage 6 automated test suite
├── test_stage7a.py              # Stage 7A automated test suite
├── test_stage7b.py              # Stage 7B automated test suite
├── test_stage8a.py              # Stage 8A automated test suite
└── src/
    ├── arch/
    │   └── x86_64/
    │       ├── boot.S           # Multiboot headers & 32-bit to 64-bit transition
    │       ├── interrupts.S     # Low-level 64-bit ISR stubs (timer, keyboard, #PF)
    │       ├── context_switch.S # Cooperative assembly context switch routine
    │       └── user.S           # switch_to_user_mode, user program & isr_user_return
    └── kernel/
        ├── io.h                 # Port I/O inline assembly (inb, outb, io_wait)
        ├── vga.h                # VGA text mode interface (colors, print_dec, print_hex)
        ├── vga.c                # VGA driver implementation
        ├── idt.h                # IDT descriptor structures & vector definitions
        ├── idt.c                # IDT table & lidt loading (#PF handler)
        ├── gdt.h                # GDT segment selectors, TSS struct & API definitions
        ├── gdt.c                # 64-bit GDT table, TSS initialization & lgdt/ltr
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
        ├── multiboot.h          # Multiboot 1 specifications and memory map structures
        ├── pmm.h                # Physical memory manager interface & frame constants
        ├── pmm.c                # 4 KiB frame bitmap allocator & reserved memory tracker
        ├── vmm.h                # Virtual memory manager interface & translation macros
        ├── vmm.c                # 4-level page table walk, mapping & TLB invalidation
        ├── heap.h               # Kernel heap allocator interface & block headers
        ├── heap.c               # Free-list allocator, kmalloc/kfree & coalescing
        ├── task.h               # Kernel task subsystem, TCB & task states
        ├── task.c               # Task management, cooperative switching & demo
        ├── scheduler.h          # Round-robin scheduler interface & statistics
        ├── scheduler.c          # Preemptive round-robin scheduler & demo tasks
        ├── user.h               # User mode subsystem interface & status block definitions
        ├── user.c               # User memory mapping, vector 0x80 gate & verification
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
python3 test_stage8a.py
```
