# Stage 13A Walkthrough: Process Creation + Executable Launch

## 1. Executive Summary

Stage 13A unites the ELF64 loader, virtual filesystem (VFS/RAMFS/PFS), address-space isolation (CR3), and timer-driven preemptive scheduler into an end-to-end user process launch pipeline. Users and the shell can now launch standalone 64-bit ELF executables residing on persistent disk storage (PFS) or in-memory storage (RAMFS). Executables run as isolated Ring 3 processes with dedicated page tables, user stacks, and kernel interrupt stacks, executing system calls and terminating cleanly with zero memory leaks.

```text
========================================================================================
                          STAGE 13A PROCESS LAUNCH ARCHITECTURE
========================================================================================

                                  Shell / User Command
                                    [ run <path> ]
                                          |
                                          v
                              VFS Mount Resolution Layer
                                   /             \
                                  v               v
                             RAMFS ("/")    PFS ("/disk")
                                                  |
                                            Block Device API
                                                  |
                                            ATA PIO Driver
                                                  |
                                           QEMU Disk (disk.img)
                                          |
                                          v
                             Read ELF into Temporary Buffer
                                          |
                                          v
                              Validate ELF64 Headers
                                (Magic, Machine, Flags)
                                          |
                                          v
                            Allocate Process Table Slot
                             (Check MAX_PROCESSES limit)
                                          |
                                          v
                             Create Private User PML4
                               (Clone Lower 1 GiB)
                                          |
                                          v
                             Inherit Caller Attributes
                               (PPID, Refcounted CWD)
                                          |
                                          v
                           Map PT_LOAD Segments into PML4
                             (W^X Permissions: RX / RW)
                                          |
                                          v
                            Allocate User Stack (0x70000000)
                           & Dedicated TSS Kernel RSP0 Stack
                                          |
                                          v
                          Free Temporary ELF Kernel Buffer
                                          |
                                          v
                            Configure Task IRETQ Frame
                         (CS=0x23, SS=0x1B, RIP=e_entry)
                                          |
                                          v
                           Timer Interrupt / Scheduler Tick
                                          |
                                          v
                           IRETQ Transition to Ring 3 (CPL 3)
                                          |
                                          v
                             User Execution & Syscalls
                            (SYS_WRITE, SYS_GETTIME, ...)
                                          |
                                          v
                                SYS_EXIT(status = 42)
                                          |
                                          v
                             Deferred Process Reaper
                         (Free PML4, Frames, Stacks, Slots)
========================================================================================
```

---

## 2. Key Architectural Implementations

### 1. Process Table Alignment & Lineage Tracking (`src/kernel/process.h`)
- Increased `MAX_PROCESSES` from 4 to 8, matching `MAX_TASKS = 8` and enabling concurrent process execution.
- Added `uint32_t ppid;` to `struct process` to record parent process lineage upon creation (`caller ? caller->pid : 0`).

### 2. Robust Process Creation Pipeline (`src/kernel/elf.c`, `src/kernel/elf.h`)
- Added `ELF_ERR_PROC_LIMIT` (-31) and implemented process table capacity pre-checks before opening or loading binaries.
- Implemented `process_create_from_elf_path(const char *path, const char *name, struct process **out_proc)`:
  - Resolves executable vnode and reads image dynamically via generic VFS/FD abstraction.
  - Dynamically creates private PML4 page directory (`vmm_create_process_pml4()`) with zero heap overhead.
  - Inherits caller's CWD atomically with `vfs_node_ref(proc->cwd)` (released upon process reaping).
  - Validates and maps `PT_LOAD` segments into user address space (`0x60000000`, `0x60001000`) with strict W^X enforcement.
  - Allocates 4 KiB user stack at `0x70000000` (stack top `0x70001000`) and private kernel interrupt stack in `.bss`.
  - Transactional rollback: any failure during header verification, memory allocation, or segment mapping triggers complete rollback (unmapping user pages, freeing allocated frames, restoring caller CR3, clearing process slot).
  - Deallocates temporary ELF buffer immediately after segment mapping is complete.

### 3. Shell Command Architecture & Lifecycle (`src/kernel/shell.c`)
- Re-architected built-in `run <path>`:
  - Enforces strict argument validation using `parse_single_path_arg`: rejects missing arguments (`Usage: run <path>`) and excess arguments (`run: too many arguments`).
  - Implements dynamic VFS path resolution with transparent fallback to `/disk/<path>` when persistent binaries are referenced.
  - Emits launch notification: `started process <PID>`.
  - Enables interrupts (`sti`), yields CPU in a bounded wait loop until `proc->state == PROCESS_TERMINATED`, and immediately calls `process_reap_terminated()`.
  - Provides descriptive diagnostics for nonexistent files, directories, process limits, and corrupt binaries.

### 4. Persistent Ring 3 Executable (`user/hello.c`) & Offline Disk Tooling (`tools/pfs_populate.py`)
- Created freestanding Ring 3 user program `user/hello.c`:
  - Validates `CPL == 3` via `%cs`.
  - Validates private writable `.data` and `.bss` memory.
  - Invokes `SYS_GETTIME` to measure kernel uptime ticks.
  - Invokes `SYS_WRITE` to print `  [ELF Ring 3] Hello from persistent ELF executable!\n`.
  - Terminates cleanly with exit status 42 via `SYS_EXIT`.
- Custom linker configuration (`user/linker.ld` with `-N` OMAGIC): produces a 1,272-byte ELF binary fitting comfortably within PFS direct block limits (4,096 bytes).
- Offline tool `tools/pfs_populate.py`: formats and populates `build/disk.img` with a valid PFS filesystem containing `/bin/hello` and `/hello`.

---

## 3. Automated Test Matrix (`test_stage13a.py`)

All 20 automated test cases pass 100%:

| Test # | Description | Result | Details |
|---|---|---|---|
| **Test 1** | Boot Integrity & 24-Row Budget | **PASS** | Kernel boots cleanly; display output within 24 rows |
| **Test 2** | Shell Command Table & Screen Budget | **PASS** | `help` command output fits within 24 rows |
| **Test 3** | Argument Parsing: Empty `run` | **PASS** | Emits `Usage: run <path>` |
| **Test 4** | Argument Parsing: Excess Arguments | **PASS** | Emits `run: too many arguments` on `run a b` |
| **Test 5** | Non-Existent Binary Execution | **PASS** | Emits `Error: File not found: /nope` |
| **Test 6** | Directory Execution Rejection | **PASS** | Emits `Error: Cannot execute directory: /bin` |
| **Test 7** | RAMFS Executable Launch | **PASS** | `run /bin/test` launches and completes with status 42 |
| **Test 8** | Corrupted Binary Rejection | **PASS** | `run /bin/bad` fails validation cleanly without crash |
| **Test 9** | Persistent ELF via Absolute Path | **PASS** | `run /disk/bin/hello` prints message and exits with status 42 |
| **Test 10** | Persistent ELF via Root Path | **PASS** | `run /disk/hello` executes and exits with status 42 |
| **Test 11** | Persistent ELF via Fallback Path | **PASS** | `run /bin/hello` transparently resolves to `/disk/bin/hello` |
| **Test 12** | Persistent ELF via Relative Path | **PASS** | `cd /disk; run bin/hello` resolves and executes |
| **Test 13** | Persistent ELF from Current Directory | **PASS** | `cd /disk/bin; run hello` resolves and executes |
| **Test 14** | Process Launch Notification | **PASS** | Emits `started process <PID>` |
| **Test 15** | Ring 3 Execution & Syscall Verification | **PASS** | Emits `[ELF Ring 3] Hello from persistent ELF executable!` |
| **Test 16** | Clean Process Termination | **PASS** | Process terminates cleanly and reclaims resources |
| **Test 17** | Memory Reclamation (Leak-Free) | **PASS** | Heap invariant preserved (`Used: 0 bytes`, `Free: 65512 bytes`) |
| **Test 18** | Repeated Execution Stability | **PASS** | 10 consecutive process executions succeed without degradation |
| **Test 19** | Process Limit Handling | **PASS** | System handles maximum process table occupancy gracefully |
| **Test 20** | Cross-Session Persistence | **PASS** | Persistent binary loads and executes across cold QEMU reboots |

---

## 4. Full Regression Verification

- `test_stage12a.py`: **8/8 PASS** (100%)
- `test_stage12b.py`: **8/8 PASS** (100%)
- `test_stage12c.py`: **10/10 PASS** (100%)
- `test_stage12d.py`: **15/15 PASS** (100%)
- `test_stage12e.py`: **15/15 PASS** (100%)
- `test_stage13a.py`: **20/20 PASS** (100%)
- **Total Stage 12A–13A Matrix**: **76/76 PASS (100%)**
- **Compiler Warnings**: 0
- **Linker Warnings**: 0
- **Boot Heap Invariant**: `Used: 0 bytes`, `Free: 65512 bytes` (100% preserved)
- **Screen Budget**: All outputs fit strictly within 24 rows
