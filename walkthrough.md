# Stage 13B Walkthrough: Process Lifecycle & Wait Subsystem

## 1. Executive Summary

Stage 13B transitions MyOS from basic fire-and-forget process creation into a complete parent/child process lifecycle. Exiting processes no longer vanish asynchronously into an auto-reaping void; instead, they transition to `PROCESS_ZOMBIE` and await their parent to collect their exit status and reap their resources via `sys_wait()` (syscall 6). Parents blocking in `wait()` consume **zero CPU cycles** via non-busy waiting integrated directly into the preemptive scheduler.

```text
========================================================================================
                          STAGE 13B PROCESS LIFECYCLE PIPELINE
========================================================================================

    Parent Process (Shell / User)
         |
         | process_create_from_elf_path()
         v
    [ PROCESS_READY / TASK_READY ] (Child allocated, PPID = Parent PID)
         |
         | Timer Tick / Scheduler dispatch
         v
    [ PROCESS_RUNNING / TASK_RUNNING ] (Child executes in Ring 3)
         |
         +-------------------------------------+
         |                                     |
         v (Parent calls wait())               v (Child calls exit(status))
    Parent checks children:               Child executes sys_exit(status):
    - Any zombie?                         1. Sets proc->exit_status = status
      YES: reap atomically, return PID    2. Sets proc->state = PROCESS_ZOMBIE
      NO:                                 3. Sets task->state = TASK_FINISHED
      - Any living children?              4. Closes all open FDs
        YES:                              5. Releases CWD reference
          proc->state = PROCESS_BLOCKED   6. Reparents living children to PID 0
          task->state = TASK_BLOCKED      7. Wakes blocked parent (if waiting)
          int $0x81 (scheduler_yield)     8. Yields CPU (scheduler deschedules)
          [0 CPU Cycles Consumed!]             |
         |                                     |
         +<------------------------------------+
         | (Parent unblocks: state -> PROCESS_READY)
         v
    Parent Reaps Child Atomically:
    1. Validates user status pointer (rejects NULL/kernel addresses: -EFAULT)
    2. Writes exit_status to caller memory: *status_ptr = child->exit_status
    3. Frees child address space (PML4, code/stack/user frames, table frames)
    4. Resets child task and process slot: child->state = PROCESS_UNUSED
    5. Returns reaped child PID to parent
========================================================================================
```

---

## 2. Key Architectural Implementations

### 1. Unified Process & Task State Machines (`process.h`, `task.h`)
- Process states:
  - `PROCESS_UNUSED`: Slot available for allocation.
  - `PROCESS_READY`: Scheduled and ready for execution.
  - `PROCESS_RUNNING`: Currently active on CPU.
  - `PROCESS_BLOCKED`: Suspended waiting for an event (e.g. child termination).
  - `PROCESS_ZOMBIE`: Terminated, retains exit status and PID until reaped by parent.
  - *Note: `PROCESS_TERMINATED` is completely eliminated.*
- Task states:
  - `TASK_UNUSED`, `TASK_READY`, `TASK_RUNNING`, `TASK_BLOCKED`, `TASK_FINISHED`.
  - The scheduler strictly skips `TASK_BLOCKED` and `TASK_FINISHED` tasks.

### 2. Zero Busy-Waiting Scheduler Integration (`scheduler.c`, `interrupts.S`)
- Yield mechanism:
  - Vector `0x81` installed in IDT as `IDT_YIELD_VECTOR` with DPL 0 (`isr_yield`).
  - `scheduler_yield()` executes `int $0x81`, pushing a 20-quadword interrupt frame identical to timer interrupts.
  - Stack pointer is handed to `scheduler_tick()`, selecting the next ready task and switching stacks cleanly.
- Blocked task protection:
  - A task in `TASK_BLOCKED` remains in `TASK_BLOCKED` across preemptive timer ticks and yields until explicitly unblocked by an event.

### 3. System Call `SYS_WAIT` (Syscall 6) (`syscall.c`, `process.c`)
- Signature: `int64_t sys_wait(int64_t child_pid, int64_t *status_ptr);`.
- Operation:
  - `child_pid == -1`: Waits for any child belonging to calling process.
  - `child_pid > 0`: Waits for a specific child PID belonging to calling process.
  - Error checking:
    - Returns `-SYSCALL_ECHILD` (-11) if caller has no children or if specified PID is not a child of caller.
    - Returns `-SYSCALL_EFAULT` (-2) if `status_ptr` is `NULL`, points to supervisor kernel memory, or points to unmapped/unwritable user memory. Pointer validation occurs **before** modifying child state or reaping.
- Atomic reaping:
  - Status is written to `status_ptr` before freeing child memory.
  - Reaping releases all PML4 page tables, code frames, stack frames, dynamic user frames, and resets the process slot to `PROCESS_UNUSED`.
  - The slot is immediately eligible for PID reuse.

### 4. Child Reparenting & Orphan Prevention (`process.c`)
- In `process_exit()`, before the exiting process transitions to `PROCESS_ZOMBIE`, it iterates over all processes.
- Any process with `child->ppid == proc->pid` is reparented: `child->ppid = 0;`.
- PID 0 (kernel / init) adopts orphaned children, ensuring they are cleanly reaped if needed.

### 5. Persistent User Binaries on Disk Image (`user/`, `tools/pfs_populate.py`)
- Created user executables compiled with custom freestanding linker script `user/linker.ld`:
  - `/bin/exit0`: Exits cleanly with status 0.
  - `/bin/exit42`: Exits cleanly with status 42.
  - `/bin/delayed_exit`: Runs a sustained computation loop across PIT timer ticks and outputs `  [delayed_exit] running computation...` before exiting with status 42.
  - `/bin/hello`: Existing Stage 13A user executable exiting with status 42.
- Pre-populated into persistent disk image `build/disk.img` under both `/bin/<name>` and `/<name>`.

### 6. Interactive Shell Architecture (`shell.c`)
- `run <path>`:
  - Calls `process_create_from_elf_path()`.
  - Prints `started process <PID>`.
  - Invokes `process_wait((int64_t)proc->pid, &status, false)` to cleanly await and reap child without polling.
  - Maintains strict 25-row screen budget.
- `waittest`:
  - Unlisted diagnostic command exercising all 11 lifecycle properties in kernel mode.

---

## 3. Automated Verification Matrix (`test_stage13b.py`)

All 9 test suites covering 26 verification checks pass 100%:

| Test Suite | Focus / Subsystem | Result | Details |
|---|---|---|---|
| **Test 1** | Build Cleanliness | **PASS** | Kernel & user ELFs build with 0 compiler and linker warnings |
| **Test 2** | In-Kernel Lifecycle Suite (`waittest`) | **PASS** | 11 unit checks passed: zombie state, exit status, double-wait rejection, non-child rejection, pointer fault defense, multi-child reaping, reparenting, zero memory leaks |
| **Test 3** | User ELF `exit(0)` Execution | **PASS** | `run /bin/exit0` executes Ring 3 binary and exits with status 0 |
| **Test 4** | User ELF `exit(42)` Execution | **PASS** | `run /bin/exit42` executes Ring 3 binary and exits with status 42 |
| **Test 5** | Non-Busy Parent Blocking (`delayed_exit`) | **PASS** | Parent blocks with 0 CPU consumption while child computes across ticks |
| **Test 6** | PID Reuse & Slot Recovery | **PASS** | Consecutive runs allocate and reuse PID 1; `ps` verifies slot cleared |
| **Test 7** | Cross-Session Persistence | **PASS** | `/disk/bin/exit42` and `/disk/bin/delayed_exit` persist across cold reboots |
| **Test 8** | Stage 9 Regression (`proctest`) | **PASS** | CR3 isolation, W^X page permissions, timer preemption intact |
| **Test 9** | Stage 10/11C Regression (`elftest`) | **PASS** | ELF64 header validation, segment loading, BSS zeroing intact |

---

## 4. Adversarial Audit Findings Resolution

| Finding | Severity | Description & Root Cause | Resolution Implemented |
|---|---|---|---|
| **F-01** | CRITICAL | **Lost-Wakeup Race in `process_wait()`**: `process_wait()` scanned children while interrupts were enabled and executed `cli` only immediately before blocking. A child could exit between the scan and blocking, causing a lost wakeup. | Entered `cli` before the child-state decision and table scan. If children exist but none are zombies, caller state and task state are transitioned to `PROCESS_BLOCKED`/`TASK_BLOCKED` while interrupts remain disabled. `scheduler_yield()` executes `sti; int $0x81` atomically. Interrupts are restored upon return. |
| **F-02** | HIGH | **Inadequate Reap Guards**: `process_reap()` did not reject invalid reap attempts on non-zombie or active processes. | Added strict guards at the start of `process_reap()` rejecting `NULL`, already reaped processes, `proc->state != PROCESS_ZOMBIE` (`PROCESS_RUNNING`, `PROCESS_READY`, `PROCESS_BLOCKED`, `PROCESS_UNUSED`), `proc == current_process`, and any process whose task is currently executing on the CPU. |
| **F-03** | MEDIUM | **Unbounded Orphan Zombie Accumulation**: Orphan processes reparented to PID 0 transition to `PROCESS_ZOMBIE`, but PID 0 never calls `wait()` on them in shell mode, potentially exhausting `MAX_PROCESSES`. | Added `is_orphan` tracking on reparenting. Implemented `process_reap_orphans()` and `process_find_free_slot()` which safely reclaim dead orphan zombies when process slots are needed, guaranteeing `MAX_PROCESSES` can never be permanently exhausted. |
| **F-04** | LOW | **User Status Pointer Integrity**: In `process_wait()`, the user-provided status buffer could theoretically become unmapped or invalid before writing exit status. | Re-validated `status` buffer immediately before writing `*status = zombie_child->exit_status`. If validation fails, `-SYSCALL_EFAULT` is returned without reaping the zombie child (leaving the child intact and waitable). |

---

## 5. Full Regression Verification Across Subsystems

- `test_stage7b.py`: **22/22 PASS** (100%)
- `test_stage8a.py`: **PASS** (100%)
- `test_stage8b.py`: **14/14 PASS** (100%)
- `test_stage9.py`: **14/14 PASS** (100%)
- `test_stage10.py`: **14/14 PASS** (100%)
- `test_stage11c.py`: **10/10 PASS** (100%)
- `test_stage12d.py`: **15/15 PASS** (100%)
- `test_stage12e.py`: **15/15 PASS** (100%)
- `test_stage13a.py`: **20/20 PASS** (100%)
- `test_stage13b.py`: **26/26 PASS** (100%)
- **Compiler Warnings**: 0
- **Linker Warnings**: 0
- **Heap Invariant**: `Used: 0 bytes`, `Free: 65512 bytes` (100% preserved)
- **Screen Budget**: All outputs fit strictly within 24 rows
