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
- **Stage 8B: System Calls (int 0x80 ABI & Pointer Validation)**
  - Minimal, robust system-call subsystem allowing Ring 3 user code to request controlled kernel services.
  - System call ABI via `int 0x80` Interrupt Gate (`IDT_FLAG_USER_INTERRUPT_GATE = 0xEE`):
    - Syscall number in `%rax`: `SYS_EXIT` (0), `SYS_WRITE` (1), `SYS_GETTIME` (2).
    - Syscall arguments in `%rdi` (arg1), `%rsi` (arg2), `%rdx` (arg3).
    - Return value in `%rax`: signed 64-bit value (`>= 0` on success, negative error code on failure).
    - Preserved user registers across system call boundary via assembly wrapper `isr_syscall`.
  - Kernel boundary defense & user pointer validation (`syscall_validate_user_buffer`):
    - Strict rejection of NULL pointers.
    - Strict rejection of kernel-space addresses (`< 0x60000000` or `>= 0x60002000`).
    - Pointer arithmetic overflow protection (`vaddr + len < vaddr`).
    - Multi-page range validation ensuring every page in `[vaddr, vaddr + len)` is mapped in the VMM with `PTE_PRESENT | PTE_USER`.
    - Returns `SYSCALL_EFAULT` (`-2`) on invalid user memory addresses without causing kernel page faults.
    - Returns `SYSCALL_ENOSYS` (`-3`) on unrecognized syscall numbers.
  - Initial system calls:
    - `SYS_EXIT` (0): Clean privilege transition from Ring 3 back to Ring 0 kernel caller via `isr_user_return`.
    - `SYS_WRITE` (1): Safely writes user-space string buffer to VGA/terminal, returning number of bytes written.
    - `SYS_GETTIME` (2): Returns current system timer tick count (`timer_get_ticks()`).
  - Ring 3 user program (`syscall_test_program`) and user-side wrappers (`user_syscall0`, `user_syscall2`):
    - Executes entirely at CPL 3, verifies CPL == 3, invokes `SYS_WRITE`, tests `SYS_GETTIME` monotonicity, verifies `-ENOSYS` on invalid syscalls, and verifies `-EFAULT` on invalid pointers.
  - Shell command: `syscalltest` (runs Ring 3 syscall verification and prints comprehensive test results).

- **Stage 9: First Real Process Management Foundation**
  - Architectural transition from cooperative/preemptive kernel tasks to real, isolated user processes (`process_t`).
  - Process Table (`proc_table`) managing up to 8 processes (`MAX_PROCESSES = 8`).
  - Process states: `PROCESS_UNUSED`, `PROCESS_READY`, `PROCESS_RUNNING`, `PROCESS_TERMINATED`.
  - Process types: `PROCESS_TYPE_KERNEL` (PID 0, boot/shell thread using boot PML4) and `PROCESS_TYPE_USER` (Ring 3 processes with private PML4 page tables).
  - True Address-Space Isolation via CR3:
    - Dedicated 4 KiB PML4 page directory allocated per process via `vmm_create_process_pml4()`.
    - Lower 1 GiB identity mapping cloned from boot PML4, guaranteeing kernel execution, interrupt servicing, and memory managers remain accessible in all address spaces.
    - Private physical frames mapped at identical user virtual addresses (`0x60000000` for user code, `0x60001000` for user stack/data).
    - Writes by Process A to VA `0x60001800` do NOT affect Process B writing to VA `0x60001800` because each process has a distinct CR3 root pointing to separate physical frames.
  - Preemptive Multitasking & Context Switching:
    - Integrated with Stage 7B 100 Hz PIT timer interrupt and round-robin scheduler.
    - Upon selecting a task in `scheduler_tick()`:
      - Switches CPU page directory root via `vmm_write_cr3(next->cr3)`.
      - Updates TSS kernel interrupt stack pointer via `gdt_set_rsp0(proc->kernel_stack_top)` so any subsequent interrupt/exception in user mode uses the process's private kernel stack.
      - Tracks active process via `current_process`.
      - Performs deferred reaping of terminated processes.
  - Process Lifecycle & Memory Reclamation:
    - `process_create()` allocates user code/stack frames, private PML4, kernel stack, and registers a user task frame.
    - `SYS_EXIT` syscall (`sys_exit()`) marks the calling user process as `PROCESS_TERMINATED` and halts the thread until descheduled.
    - Deferred reaper (`process_reap_terminated()`) invoked by the scheduler unmaps user pages, frees physical frames (user code, user stack, private PML4, kernel stack), resets process and task slots for PID reuse with zero memory leaks.
  - User-Space Process Syscalls:
    - Ring 3 processes issue `SYS_WRITE` and `SYS_GETTIME` via `int 0x80`, returning results into user registers via original hardware `iretq` frame.
    - Termination via `SYS_EXIT` triggers clean kernel transition and task descheduling.
  - Shell Commands:
    - `ps`: Displays process table status (PID, STATE, TYPE, CR3, NAME).
    - `proctest`: Automated in-kernel test creating Process A and Process B, executing concurrent Ring 3 workloads, validating VA isolation at `0x60001800`, timer preemption, user syscalls, memory reclamation (0 leaks), and PID reuse.
  - Architectural Boundaries & Scope:
    - Deliberately does NOT implement an ELF loader (binaries are compiled in kernel and loaded at `0x60000000`), `fork()`/`exec()`, signals, pipes, or a virtual filesystem (VFS).

- **Stage 10: ELF64 Executable Program Loader**
  - Freestanding 64-bit ELF (`ET_EXEC`) parser, validator, segment loader, and execution engine.
  - Strict ELF Header Validation:
    - Magic bytes (`\x7fELF`), 64-bit class (`ELFCLASS64`), little-endian (`ELFDATA2LSB`), current version (`EV_CURRENT`), AMD x86-64 machine type (`EM_X86_64`), executable type (`ET_EXEC`), and non-zero entry point (`e_entry`).
    - Program header table bounds checking (`phoff`, `phentsize`, `phnum`), rejecting truncated or malformed headers.
  - Strict `PT_LOAD` Segment Verification:
    - Virtual addresses bounded strictly to user space `[0x40000000, 0x80000000)`.
    - Arithmetic overflow protection (`p_vaddr + p_memsz >= p_vaddr`).
    - Kernel protection: strictly rejects mappings overlapping kernel low memory (`< 0x40000000`) or kernel heap (`0x50000000..0x50010000`).
    - File boundary enforcement (`p_offset + p_filesz <= file_size`) and memory size constraint (`p_filesz <= p_memsz`).
    - Page-level segment overlap detection and rejection.
  - Memory Permissions & W^X Enforcement:
    - Allocates dedicated physical 4 KiB frames via PMM for each segment page.
    - Maps pages into the process's private PML4 directory with user privilege (`PTE_PRESENT | PTE_USER`).
    - Read-only protection for executable code segments (`PF_X` without `PF_W`), read-write protection for data/BSS segments (`PF_W`).
    - BSS zero-initialization (`p_memsz > p_filesz`) and page-tail zero padding.
  - Dedicated User Stack & Register State:
    - 4 KiB user stack allocated at `0x70000000` (`PTE_PRESENT | PTE_USER | PTE_WRITABLE`), stack top at `0x70001000` aligned to 16 bytes per System V ABI.
    - IRETQ frame configured for Ring 3 entry (`SS=0x1B`, `RSP=0x70001000`, `RFLAGS=0x202`, `CS=0x23`, `RIP=e_entry`).
  - Process Lifecycle & Dynamic Resource Management:
    - Tracks up to 16 dynamically allocated user frames per process in `user_frames[]`.
    - Deferred reaper automatically unmaps and frees all user segment frames, stack frames, private PML4, and kernel stack, achieving 100% leak-free execution.
  - Embedded User-Space Binary Pipeline:
    - Freestanding user executable compiled from `user/start.S` and `user/test_program.c` with custom linker script `user/linker.ld`.
    - Embedded into kernel image via `src/kernel/elf_image.S` using GNU Assembler `.incbin`.
    - Ring 3 program verifies CPL 3, reads/writes `.data` and `.bss`, invokes `SYS_GETTIME`, executes `SYS_WRITE` ("Hello from loaded ELF64 executable!"), and exits with code 42 via `SYS_EXIT`.
  - In-Kernel Test Suite & Shell Command:
    - `elftest`: Runs comprehensive in-kernel test suite with 10 negative validation cases (bad magic, 32-bit class, big-endian, bad machine, non-exec type, bad entry, kernel overlap, heap overlap, address overflow, truncated header) followed by positive ELF loading, Ring 3 execution, exit code 42 verification, and frame reclamation check.
  - Non-Goals & Scope Limits:
    - No dynamic linker (`.so`), no filesystem/VFS, no `fork()` or `exec()` syscall.

- **Stage 11A: Virtual Filesystem (VFS) Core and In-Memory RAMFS**
  - Filesystem-Independent VFS Core Abstraction (`vfs.h`, `vfs.c`):
    - Abstract node representation (`vfs_node_t`) with operation table (`vfs_node_ops_t`: `lookup`, `create`, `mkdir`, `read`, `write`).
    - Filesystem mount abstraction (`vfs_fs_t`, `vfs_fs_ops_t`: `mount`, `unmount`).
    - Standardized node types: `VFS_NODE_FILE`, `VFS_NODE_DIRECTORY`, and reserved `VFS_NODE_DEVICE`.
    - Canonical error codes: `VFS_OK` (0), `VFS_ERR_NOT_FOUND` (-1), `VFS_ERR_INVALID_ARG` (-2), `VFS_ERR_EXISTS` (-3), `VFS_ERR_NOT_DIR` (-4), `VFS_ERR_IS_DIR` (-5), `VFS_ERR_NO_MEM` (-6), `VFS_ERR_NOT_SUPPORTED` (-7), `VFS_ERR_NAME_TOO_LONG` (-8), `VFS_ERR_PATH_TOO_LONG` (-9).
    - String error decoder: `vfs_strerror()`.
  - In-Memory RAM Filesystem (`ramfs.h`, `ramfs.c`) mounted at root `/`:
    - Linked list directory children model (`ramfs_entry_t`).
    - Deterministic path resolution: absolute paths starting with `/`, collapsing consecutive slashes, validating component names (`<= 32` bytes), path length limit (`<= 256` bytes), rejecting non-directory intermediate traversal.
    - Strict non-sparse write enforcement: sequential appends and in-place overwrites supported; writes attempting to leave gaps (`offset > size`) rejected with `VFS_ERR_NOT_SUPPORTED`.
    - Static Root Image & Heap Backward Compatibility:
      - Initial root structure (`/`, `/bin`, `/etc`, `/readme.txt` with content `"Hello from MyOS RAMFS!\n"`) statically allocated in kernel image.
      - Keeps kernel heap untouched (`Used: 0 bytes`) at boot to preserve Stage 6, 7A, and 8A regression invariants.
    - Dynamic Allocations for Runtime Changes:
      - Runtime file/directory creations (`vfs_create`, `vfs_mkdir`) and file write expansions allocate from kernel heap via `kmalloc`/`kfree`.
      - Failures cleanly roll back with zero leaked memory.
  - Shell Command:
    - `vfstest`: Runs 10-step in-kernel verification suite testing VFS initialization, root mount, root lookup, file lookup, file read, file creation, directory creation, write/read roundtrip, path validation, and heap stability.
  - Scope Boundaries:
    - No disk or storage drivers, no block device layer, no FAT/ext2/ext4, no POSIX syscall ABI yet.
- **Stage 11B — File Descriptors + Open/Read/Write/Close (Completed)**:
  - Three-Tier File Abstraction:
    - Layer 1 (`vfs_node_t`): Underlying filesystem object in RAMFS.
    - Layer 2 (`open_file_t`): Active file session with independent cursor `offset`, `flags` (`O_RDONLY`, `O_WRONLY`, `O_RDWR`), `refcount`, and `type` (`OPEN_FILE_VFS`, `OPEN_FILE_CONSOLE`).
    - Layer 3 (`file descriptor`): Integer handle in per-process table `fds[MAX_PROCESS_FDS]` (0..15).
  - Standard Streams:
    - FDs 0 (stdin), 1 (stdout), and 2 (stderr) initialized for all processes with statically pre-allocated console stream objects (0 heap allocation at boot).
    - Writes to stdout/stderr route to VGA text subsystem; reads from stdin return EOF (0).
  - System Calls:
    - `SYS_OPEN` (3): Resolves VFS path, allocates lowest available FD (`fd >= 3`), returns FD or negative error.
    - `SYS_READ` (4): Reads from open FD starting at current `offset`, advances cursor by bytes read, validates user buffer is mapped and writable in active PML4. Rejects directory read with `EISDIR` (-7).
    - `SYS_WRITE` (1): Seamless dual-mode dispatcher supporting both modern 3-arg `sys_write(fd, buf, count)` and legacy 2-arg `sys_write(buf, count)` without regressions. Enforces `O_WRONLY` permission (`EACCES` on read-only FDs).
    - `SYS_CLOSE` (5): Closes FD, decrements `refcount`, frees dynamic `open_file_t`, and clears slot for immediate reuse.
  - Security & Isolation:
    - Strict path copying and pointer validation (`syscall_copy_user_path`, `syscall_validate_user_buffer`, `syscall_validate_writable_user_buffer`).
    - Dedicated per-process FD tables: operations in Process A do not mutate or expose descriptors in Process B.
    - Clean process teardown: `fd_close_all()` reclaims all open files on process exit or deferred reaping.
  - Verification & Shell:
    - `fdtest`: In-kernel verification suite testing 10 architectural checkpoints.
- **Stage 11C: Filesystem-Backed ELF Execution**
  - Storage-Independent ELF Execution Pipeline:
    - Decoupled `elf.c` from compile-time embedded linker symbols (`_binary_test_program_elf_start`), transforming the loader into a pure, storage-agnostic subsystem operating on buffers read dynamically from the VFS.
    - Added `process_exec_path(const char *path, const char *name)` and `elf_exec_path(const char *path, const char *name)` in `elf.c` / `elf.h`.
    - Internal execution flow:
      1. Open target executable via `fd_open(current_process, path, O_RDONLY)`.
      2. Verify target is a regular file (reject directories with `ELF_ERR_IS_DIR`).
      3. Query file size via `fd_get_size(current_process, fd)`.
      4. Allocate temporary kernel buffer via `kmalloc(size)`.
      5. Read entire executable into memory via `fd_read(current_process, fd, buf, size)`.
      6. Close file descriptor via `fd_close(current_process, fd)` immediately.
      7. Validate ELF64 headers and segments (`elf_validate`).
      8. Instantiate isolated Ring 3 process (`process_create_from_elf`).
      9. Free temporary kernel buffer via `kfree(buf)` prior to scheduling.
  - VFS and RAMFS Executable Population:
    - Boot-time static initialization of `/bin/test` referencing freestanding user ELF bytes, size, and permissions (`0755`).
    - Boot-time static initialization of `/bin/bad` with corrupted header bytes and size 16 (`0644`) for negative testing.
    - Boot-time static descriptors guarantee zero heap allocations during boot (`Used: 0 bytes`, `Free: 65512 bytes`), strictly preserving Stage 6 heap invariants.
  - File Descriptor Inspection Helper:
    - Added `fd_get_size(void *proc_ptr, int fd)` in `file.h` / `file.c` to retrieve open file size directly from underlying `vfs_node_t` (`-SYSCALL_EBADF` on invalid descriptors).
  - Multi-Process Concurrency & Memory Isolation:
    - Executing `/bin/test` concurrently spawns independent processes with distinct PIDs, dedicated CR3 roots, and private physical frames.
    - Verified complete memory reclamation upon process termination (exit code 42) via deferred reaper (`process_reap_terminated`).
  - Interactive Shell Execution:
    - Added built-in shell command `run <path>`:
      - Usage reporting when path argument is omitted (`Usage: run <path>`).
      - Informative diagnostics on missing files (`File not found: <path>`), directories (`Cannot execute directory: <path>`), and malformed binaries (`ELF load failed: <path> (<error>)`).
      - Compact 2-column layout in `help` to strictly maintain the 25-row screen display budget.
  - Automated Verification Suite:
    - `test_stage11c.py`: Comprehensive 10-test automated suite verifying command usage, non-existent file rejection, directory execution rejection, corrupted ELF rejection, `/bin/test` execution to exit code 42, dual-process concurrency/isolation, `run /bin/test` shell execution, storage independence (zero embedded symbols in `elf.o`), and post-execution heap stability.
- **Stage 11D: Shell Filesystem Operations (`ls`, `cat`, `touch`, `mkdir`)**
  - Filesystem-Independent Directory Iteration (`vfs.h`, `vfs.c`):
    - Added `vfs_dirent_t` representing an abstract directory entry with `name[VFS_NAME_MAX]`, `type` (`VFS_NODE_FILE`, `VFS_NODE_DIRECTORY`), and `size`.
    - Added `int (*readdir)(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent)` operation to `vfs_node_ops_t`.
    - Implemented public VFS API `vfs_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent)` returning `VFS_OK` (0), `VFS_EOF` (1), or negative error codes (`VFS_ERR_NOT_DIR`, `VFS_ERR_INVALID`, `VFS_ERR_NOT_SUPPORTED`).
  - RAMFS Directory Iteration (`ramfs.c`):
    - Implemented `ramfs_readdir()` traversing directory children linked list up to `index` without dynamic heap allocations.
    - Fully decoupled from the shell: the shell never includes `ramfs.h` or accesses RAMFS node internals directly.
  - Interactive Shell Filesystem Commands (`shell.c`):
    - `ls [path]`: Lists directory contents via `vfs_lookup` and `vfs_readdir`, appending `/` to directory entries (e.g. `bin/`, `etc/`) to distinguish them from regular files. Defaults to root `/` if path argument is omitted. Rejects non-directory targets and nonexistent paths with clear diagnostics.
    - `cat <path>`: Displays file contents using the File Descriptor abstraction (`fd_open`, `fd_read`, `fd_close`). Reads in bounded 128-byte chunks, outputs bytes safely via `vga_putc`, cleanly handles binary files (`/bin/test`) and empty files, rejects directories (`cat: <path>: Is a directory`), and guarantees descriptor closure on all paths.
    - `touch <path>`: Creates a new empty regular file (`size = 0`) via `vfs_create()`. Rejects duplicate names (`touch: cannot touch '<path>': File already exists`), missing parent directories, and non-directory parents.
    - `mkdir <path>`: Creates a new empty directory via `vfs_mkdir()`. Rejects existing entries (`mkdir: cannot create directory '<path>': File exists`), missing parent directories, and non-directory parents.
  - Strict Argument Validation:
    - Added `parse_single_path_arg()` rejecting empty paths and trailing excess arguments (e.g. `touch /a /b`, `mkdir /a /b`, `cat /a /b`, `ls /a /b`) with standard `Usage:` notices.
  - Screen Budget Compliance:
    - 29 built-in shell commands formatted in a 2-column layout in `help`, occupying only 16 screen rows within the 25-row VGA screen limit.
  - Automated Verification Suite:
    - `test_stage11d.py`: 12-test automated suite covering all 28 stage requirements, negative validation, stress testing, heap stability, and coexistence with Stages 1–11C.
- **Stage 11E — Process Current Working Directory + Path Resolution + File Lifecycle (Completed)**:
  - Per-Process Current Working Directory (`cwd`):
    - Added `vfs_node_t *cwd;` to `struct process` in `process.h`.
    - PID 0 and all spawned user processes initialize `cwd` to root (`/`) with explicit reference counting (`vfs_node_ref`).
    - Added `process_set_cwd(process_t *proc, vfs_node_t *new_dir)` in `process.c`: validates directory type (`VFS_NODE_DIRECTORY`), increments `ref_count` on `new_dir`, unreferences prior `proc->cwd`, and assigns new working directory atomically.
    - Released process `cwd` reference cleanly in `process_reap_terminated_ex` during deferred process reaping.
  - VFS Hierarchical Navigation and Relative Path Resolution:
    - Added non-owning back-pointer `struct vfs_node *parent;` to `vfs_node_t` in `vfs.h` (root parent points to root).
    - Added intrusive reference counting with `vfs_node_ref(node)` and `vfs_node_unref(node)` in `vfs.c`. Invokes `ops->release` when `ref_count` drops to 0.
    - Implemented `vfs_lookup_from(start_node, path, out_node)`: resolves absolute paths (starting with `/`) from root and relative paths from `start_node`. Supports `.` (stay) and `..` (traverse to parent), clamped at root (`/`'s parent is `/`).
    - Preserved Stage 11A strict absolute path validation on `vfs_lookup()`.
    - Added `vfs_get_path(node, buf, size)` in `vfs.c`: climbs parent pointers to reconstruct canonical absolute path without heap allocations.
    - Added `vfs_create_from`, `vfs_mkdir_from`, and `vfs_unlink_from` supporting relative and absolute path dispatch.
  - File Unlinking & Deferred Lifecycle:
    - Added `.unlink` and `.release` function pointers to `vfs_node_ops_t`.
    - Implemented `ramfs_unlink` and `ramfs_release` in `ramfs.c`: validates target is regular file (rejects directories with `VFS_ERR_IS_DIR`), unlinks directory entry from parent list, frees dirent, and marks node `unlinked = true`.
    - Dual-condition destruction invariant: unlinked nodes with `ref_count == 0` are destroyed immediately; unlinked nodes with `ref_count > 0` (e.g. held open by active file descriptors) defer memory destruction until the last `fd_close` calls `vfs_node_unref`.
  - File Descriptor Integration (`file.c`):
    - Updated `fd_open` to resolve paths via `vfs_lookup_from(proc->cwd ? proc->cwd : vfs_get_root(), path, &node)` and acquire node reference (`vfs_node_ref(node)`).
    - Updated `fd_close` and `fd_close_all` to release node reference (`vfs_node_unref(of->node)`) upon destroying dynamic `open_file_t` objects.
  - Shell Filesystem Commands & Enhancements (`shell.c`):
    - `pwd`: Prints process's current working directory canonical path using `vfs_get_path`.
    - `cd <path>`: Changes working directory, supporting relative and absolute paths, `.`, `..`, and root clamping. Rejects nonexistent paths, regular files, and excess arguments while preserving CWD and zero memory leaks.
    - `rm <path>`: Deletes regular files, supporting relative and absolute paths. Rejects directories, root (`/`), `.`, and `..` with clear diagnostics.
    - Updated existing commands (`ls`, `cat`, `touch`, `mkdir`, and `run`) to resolve relative paths against `proc->cwd`.
  - Screen Budget Compliance:
    - 32 built-in commands arranged in a 2-column layout in `help`, occupying 17 screen rows within the 25-row VGA screen limit.
    - `about` displays verified system status across 21 rows within the 25-row limit.
  - Automated Verification Suite:
    - `test_stage11e.py`: 12-test automated verification suite covering boot line budget, initial `pwd`, `cd`, relative navigation, error handling, relative directory creation, relative file ops, `rm` unlinking, `rm` error cases, relative `run test`, repeated stress/heap stability, and multi-subsystem coexistence.

- **Stage 12A: ATA PIO Disk Driver (Primary Master, LBA28, Sector I/O)**
  - Educational ATA PIO Driver Architecture (`src/kernel/ata.h`, `src/kernel/ata.c`):
    - Dedicated polling-based driver for Primary ATA channel Master device (`0x1F0..0x1F7`, control port `0x3F6`).
    - 28-bit Logical Block Addressing (`LBA <= 0x0FFFFFFF`), 512-byte sector size (256 16-bit words).
    - Added 16-bit I/O primitives `inw()` and `outw()` to `src/kernel/io.h` via GCC inline assembly.
  - Driver Protocol & Error Handling:
    - Bounded iteration timeouts (`ATA_TIMEOUT_COUNT = 2000000`) and 400ns delays (`ata_delay()`) via Alternate Status port `0x3F6`.
    - Polling checks Alternate Status `0x3F6` to avoid spurious interrupt clears and controller state corruption.
    - Floating bus detection (`0xFF` / `0x00`) and non-ATA signature detection (`cl != 0 || ch != 0`) cleanly handles absent drive without hangs.
    - Write synchronization: waits for BSY to clear after the 256th word transfer before issuing `ATA_CMD_FLUSH_CACHE` (0xE7).
    - Status error inspection: checks `ATA_SR_ERR` and `ATA_SR_DF` on reads, writes, and cache flushes.
  - Public Driver API:
    - `ata_init()`: Silent probe and initialization at boot (0 rows printed, preserving 24-row boot banner).
    - `ata_identify()`: Issues `0xEC`, validates LBA capability (word 49 bit 9), parses LBA28 sector count (words 60-61), and extracts 40-character ASCII model string with byte-swapping and trailing space trimming.
    - `ata_read_sector(lba, buffer)`: Reads 512 bytes into buffer via command `0x20`.
    - `ata_write_sector(lba, buffer)`: Writes 512 bytes from buffer via command `0x30` and flushes cache via `0xE7`.
    - Informational getters: `ata_is_present()`, `ata_get_sector_count()`, `ata_get_sector_size()`, `ata_get_model()`.
  - Shell Commands (`src/kernel/shell.c`):
    - `diskinfo`: Displays Primary Master ATA drive status (Channel, Device, Present, Model, Sector Size, LBA28 Sectors, Capacity in MB). Cleanly reports `Present: No` if drive is absent.
    - `disktest`: Non-destructive verification on reserved sector `ATA_TEST_LBA = 8`:
      1. Negative parameter validation (NULL buffer and out-of-bounds LBA rejection).
      2. Reads original sector into backup buffer.
      3. Writes deterministic test pattern `(uint8_t)(i ^ 0xA5)`.
      4. Reads back and verifies all 512 bytes.
      5. Restores original sector data and verifies complete restoration.
  - Build System & QEMU Integration (`Makefile`):
    - Dedicated 32 MiB raw zero-filled test disk `build/disk.img` generated automatically.
    - `make run` attaches `-hda $(DISK_IMG)`.
    - Full absence support: kernel boots cleanly if QEMU is launched without `-hda`.
  - Screen Budget Compliance:
    - 34 built-in commands formatted in a 2-column layout in `help`, occupying 18 screen rows within the 25-row limit.
    - `about` displays verified system status across 22 rows within the 25-row limit.
  - Automated Verification Suite:
    - `test_stage12a.py`: 8-test automated verification suite covering silent boot, screen budget, disk geometry, pattern read/write/verify/restore, idempotence, drive absence, shell command layout, and coexistence with Stages 1–11E.

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
├── test_stage8b.py              # Stage 8B automated test suite
├── test_stage9.py               # Stage 9 automated test suite
├── test_stage10.py              # Stage 10 automated test suite
├── test_stage11a.py             # Stage 11A automated test suite
├── test_stage11b.py             # Stage 11B automated test suite
├── test_stage11c.py             # Stage 11C automated test suite
├── test_stage11d.py             # Stage 11D automated test suite
├── user/
│   ├── linker.ld                # User ELF linker script (PT_LOAD segments at 0x60000000)
│   ├── start.S                  # User entry point (_start) with int 0x80 SYS_EXIT
│   └── test_program.c           # Ring 3 user test program (CPL 3, BSS/data, syscalls)
└── src/
    ├── arch/
    │   └── x86_64/
    │       ├── boot.S           # Multiboot headers & 32-bit to 64-bit transition
    │       ├── interrupts.S     # Low-level 64-bit ISR stubs (timer, keyboard, #PF)
    │       ├── context_switch.S # Cooperative assembly context switch routine
    │       └── user.S           # switch_to_user_mode, user program, isr_user_return & isr_syscall
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
        ├── syscall.h            # System call numbers, error codes & status block definitions
        ├── syscall.c            # C syscall dispatcher, sys_write, sys_gettime & validation
        ├── process.h            # Process table, process lifecycle & isolation test interface
        ├── process.c            # Process management, CR3 allocation, reaping & isolation
        ├── elf.h                # ELF64 structures, constants, loader & test API
        ├── elf.c                # ELF validator, segment loader, BSS zeroing & test suite
        ├── elf_image.S          # Embedded user ELF binary (.incbin)
        ├── vfs.h                # Virtual filesystem (VFS) interface, node ops & limits
        ├── vfs.c                # VFS core, root mount, path resolution & test suite
        ├── ramfs.h              # In-memory RAMFS factory & lifecycle prototypes
        ├── ramfs.c              # In-memory RAM filesystem implementation & operations
        ├── file.h               # File descriptor table, open_file objects & API
        ├── file.c               # FD table implementation, standard streams & tests
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
python3 test_stage11d.py
```
