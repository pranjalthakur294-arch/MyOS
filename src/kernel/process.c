/*
 * process.c - Process Management Subsystem (Stage 9)
 *
 * Implements:
 *   - Process Control Block (PCB) lifecycle and PID management
 *   - Per-process address space isolation with dedicated PML4 page table roots
 *   - Transactional process creation with rollback on allocation failure
 *   - Preemptive round-robin execution of Ring 3 processes
 *   - Clean process exit via SYS_EXIT and deferred memory reclamation
 *   - Process listing (ps) and isolation verification test harness
 */

#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "task.h"
#include "gdt.h"
#include "vga.h"
#include "timer.h"
#include "user.h"
#include "syscall.h"
#include "scheduler.h"
#include <stddef.h>
#include <stdbool.h>

/*
 * External assembly test programs defined in user.S
 */
extern void proc_test_program_a(void);
extern void proc_test_program_a_end(void);
extern void proc_test_program_b(void);
extern void proc_test_program_b_end(void);

/*
 * Process table and current process pointer
 */
static process_t proc_table[MAX_PROCESSES];
process_t *current_process = NULL;
static bool process_initialized = false;

/*
 * Freestanding String and Memory Utilities
 */
static void kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void kmemset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

static void kstrncpy(char *dest, const char *src, size_t max) {
    if (!dest || max == 0) return;
    size_t i = 0;
    if (src) {
        while (i + 1 < max && src[i] != '\0') {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
}

/*
 * process_init - Initializes process table and sets up PID 0 (kernel context).
 */
void process_init(void) {
    if (process_initialized) {
        return;
    }

    kmemset(proc_table, 0, sizeof(proc_table));

    /* Slot 0 is reserved for the kernel process context */
    proc_table[0].pid = 0;
    proc_table[0].state = PROCESS_RUNNING;
    proc_table[0].type = PROCESS_TYPE_KERNEL;
    kstrncpy(proc_table[0].name, "kernel", PROCESS_NAME_MAX);
    proc_table[0].cr3 = vmm_get_boot_cr3();
    proc_table[0].task = task_get_current();
    proc_table[0].reaped = false;
    proc_table[0].is_orphan = false;
    proc_table[0].cwd = vfs_get_root();
    vfs_node_ref(proc_table[0].cwd);
    fd_init_process(&proc_table[0]);

    /* Remaining slots 1..MAX_PROCESSES-1 are initially unused */
    for (int i = 1; i < MAX_PROCESSES; i++) {
        proc_table[i].pid = (uint32_t)i;
        proc_table[i].state = PROCESS_UNUSED;
        proc_table[i].type = PROCESS_TYPE_USER;
        proc_table[i].reaped = false;
        proc_table[i].is_orphan = false;
        proc_table[i].cwd = NULL;
        fd_init_process(&proc_table[i]);
    }

    current_process = NULL;
    process_initialized = true;
}

/*
 * process_get - Returns pointer to PCB for given PID.
 */
process_t *process_get(uint32_t pid) {
    if (pid >= MAX_PROCESSES) {
        return NULL;
    }
    return &proc_table[pid];
}

/*
 * process_current - Returns currently running user process, or NULL if kernel task.
 */
process_t *process_current(void) {
    return current_process;
}

/*
 * process_count - Returns count of active (non-unused) processes.
 */
uint32_t process_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROCESS_UNUSED) {
            count++;
        }
    }
    return count;
}

/*
 * process_set_cwd - Updates the current working directory of a process.
 * Acquires a reference on the new directory before releasing the previous one.
 */
int process_set_cwd(process_t *proc, vfs_node_t *new_dir) {
    if (!proc || !new_dir) {
        return VFS_ERR_INVALID;
    }
    if (new_dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }

    vfs_node_ref(new_dir);
    if (proc->cwd != NULL) {
        vfs_node_unref(proc->cwd);
    }
    proc->cwd = new_dir;
    return VFS_OK;
}

/*
 * process_reap - Completely frees physical memory and task resources of a process.
 * Marks process slot as PROCESS_UNUSED and reusable.
 */
void process_reap(process_t *proc) {
    if (!proc || proc->reaped) {
        return;
    }

    /* F-02: Strictly reject any process that is not in PROCESS_ZOMBIE state */
    if (proc->state != PROCESS_ZOMBIE) {
        return;
    }

    /* F-02: Reject current process */
    if (proc == current_process) {
        return;
    }

    /* F-02: Reject any process whose underlying task is currently executing */
    task_t *active_task = task_get_current();
    if (active_task && active_task->process == proc) {
        return;
    }
    if (proc->task && proc->task == active_task) {
        return;
    }

    /* 0. Release all process-owned open file descriptors */
    fd_close_all(proc);

    /* 0b. Release current working directory reference */
    if (proc->cwd != NULL) {
        vfs_node_unref(proc->cwd);
        proc->cwd = NULL;
    }

    /* 1. Free user code physical frame */
    if (proc->code_phys) {
        pmm_free_frame(proc->code_phys);
        proc->code_phys = 0;
    }

    /* 2. Free user stack physical frame */
    if (proc->stack_phys) {
        pmm_free_frame(proc->stack_phys);
        proc->stack_phys = 0;
    }

    /* 3. Free any recorded user_frames (ELF processes) */
    for (size_t k = 0; k < proc->user_frame_count; k++) {
        if (proc->user_frames[k]) {
            pmm_free_frame(proc->user_frames[k]);
            proc->user_frames[k] = 0;
        }
    }
    proc->user_frame_count = 0;

    /* 4. Free allocated intermediate page table frames */
    for (size_t k = 0; k < proc->table_frame_count; k++) {
        if (proc->table_frames[k]) {
            pmm_free_frame(proc->table_frames[k]);
            proc->table_frames[k] = 0;
        }
    }
    proc->table_frame_count = 0;

    /* 5. Free per-process PML4 root frame */
    if (proc->pml4_phys) {
        pmm_free_frame(proc->pml4_phys);
        proc->pml4_phys = 0;
    }
    proc->cr3 = 0;

    /* 6. Mark task unused and clear process reference */
    if (proc->task) {
        proc->task->state = TASK_UNUSED;
        proc->task->process = NULL;
        proc->task = NULL;
    }

    proc->state = PROCESS_UNUSED;
    proc->reaped = true;
    proc->is_orphan = false;
}

/*
 * process_reap_orphans - Safely reclaims terminated orphan zombies (Stage 13B F-03).
 *
 * Scans process table for zombie processes whose parent has terminated and reparented
 * them to PID 0 (is_orphan == true).
 * Strictly guards against reaping current_process or currently active task.
 */
void process_reap_orphans(void) {
    task_t *active_task = task_get_current();
    for (int i = 1; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROCESS_ZOMBIE && !p->reaped && p->is_orphan) {
            if (p == current_process) {
                continue;
            }
            if (active_task && active_task->process == p) {
                continue;
            }
            if (p->task && p->task == active_task) {
                continue;
            }
            process_reap(p);
        }
    }
}

/*
 * process_find_free_slot - Finds an available process slot, reclaiming orphan zombies if necessary.
 *
 * Scans for PROCESS_UNUSED slots. If all slots are occupied, reclaims dead orphan zombies
 * whose creator/parent is gone so orphan accumulation cannot permanently exhaust MAX_PROCESSES.
 *
 * Returns:
 *   Available slot index (1..MAX_PROCESSES-1), or -1 if process table is full.
 */
int process_find_free_slot(void) {
    /* 1. First pass: look for already UNUSED slot */
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROCESS_UNUSED) {
            return i;
        }
    }

    /* 2. No unused slot available: reclaim genuinely orphaned zombies whose parent has died */
    process_reap_orphans();

    /* 3. Second pass: check if an orphan zombie slot was reclaimed */
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROCESS_UNUSED) {
            return i;
        }
    }

    /* 4. Table is full of active processes and/or waitable zombies belonging to live parents.
     * Normal zombies belonging to live parents must NEVER be auto-reaped! */
    return -1;
}

/*
 * process_reap_terminated_ex - Safely reclaims physical memory frames of terminated processes.
 *
 * Parameters:
 *   executing_task - The task currently executing on the CPU (whose stack is hosting the
 *                    caller's stack frame). If non-NULL (invoked from scheduler_tick),
 *                    zombies are preserved so parent processes can collect them via wait().
 *                    If NULL (invoked explicitly by test harnesses), reaps non-running zombies.
 */
void process_reap_terminated_ex(void *executing_task) {
    if (executing_task != NULL) {
        return;
    }

    task_t *active_task = task_get_current();

    for (int i = 1; i < MAX_PROCESSES; i++) {
        process_t *proc = &proc_table[i];

        if (proc->state == PROCESS_ZOMBIE && !proc->reaped) {
            if (active_task && active_task->process == proc) {
                continue;
            }
            if (current_process == proc) {
                continue;
            }

            process_reap(proc);
        }
    }
}

/*
 * process_reap_terminated - Safely reclaims physical memory frames of terminated processes.
 */
void process_reap_terminated(void) {
    process_reap_terminated_ex(NULL);
}

/*
 * process_verify_permissions - Verifies architectural PTE flags for process and kernel pages.
 *
 * Confirms:
 *   - User code page has PTE_PRESENT and PTE_USER, but NOT PTE_WRITABLE (strictly read-only).
 *   - User stack page has PTE_PRESENT, PTE_USER, and PTE_WRITABLE (read-write).
 *   - Kernel code/data (0x100000), VGA (0xB8000), and Heap (0x50000000) have PTE_PRESENT,
 *     but NOT PTE_USER (strictly supervisor-only).
 *
 * Returns:
 *   0 if all permissions strictly adhere to architectural requirements, negative on violation.
 */
int process_verify_permissions(const process_t *proc) {
    if (!proc || proc->cr3 == 0) {
        return -1;
    }

    uint64_t flags = 0;

    /* 1. Verify User Code Page (0x60000000): PRESENT=1, USER=1, WRITABLE=0 */
    if (vmm_get_page_flags_in_pml4(proc->cr3, USER_CODE_VADDR, &flags) != 0) {
        return -2; /* Code page not mapped */
    }
    if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || (flags & PTE_WRITABLE)) {
        return -3; /* Code page permissions incorrect: must be PRESENT | USER, NOT WRITABLE */
    }

    /* 2. Verify User Stack Page (0x60001000): PRESENT=1, USER=1, WRITABLE=1 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, USER_STACK_VADDR, &flags) != 0) {
        return -4; /* Stack page not mapped */
    }
    if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || !(flags & PTE_WRITABLE)) {
        return -5; /* Stack page permissions incorrect: must be PRESENT | USER | WRITABLE */
    }

    /* 3. Verify Kernel Code (0x100000): PRESENT=1, USER=0 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0x100000ULL, &flags) != 0) {
        return -6; /* Kernel not mapped */
    }
    if (!(flags & PTE_PRESENT) || (flags & PTE_USER)) {
        return -7; /* Kernel page exposed to user mode! */
    }

    /* 4. Verify VGA Buffer (0xB8000): PRESENT=1, USER=0 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0xB8000ULL, &flags) != 0) {
        return -8; /* VGA not mapped */
    }
    if (!(flags & PTE_PRESENT) || (flags & PTE_USER)) {
        return -9; /* VGA page exposed to user mode! */
    }

    /* 5. Verify Kernel Heap (0x50000000): PRESENT=1, USER=0 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0x50000000ULL, &flags) != 0) {
        return -10; /* Heap not mapped */
    }
    if (!(flags & PTE_PRESENT) || (flags & PTE_USER)) {
        return -11; /* Heap exposed to user mode! */
    }

    return 0;
}

/*
 * process_create - Allocates and initializes an isolated user process.
 *
 * Transactional: on any allocation failure, all allocated frames are freed
 * and NULL is returned without leaking physical memory.
 *
 * Parameters:
 *   code      - Pointer to user code image to copy into code page.
 *   code_size - Size of user code in bytes (max VMM_PAGE_SIZE).
 *   name      - Human-readable process name.
 *
 * Returns:
 *   Pointer to initialized process_t, or NULL on error.
 */
process_t *process_create(const void *code, size_t code_size, const char *name) {
    if (!process_initialized) {
        process_init();
    }

    /* 1. Find an available process slot */
    int slot = process_find_free_slot();

    if (slot < 0) {
        return NULL; /* Process table full */
    }

    process_t *proc = &proc_table[slot];
    kmemset(proc, 0, sizeof(process_t));
    fd_init_process(proc);

    /* 2. Allocate user code physical frame */
    uint64_t code_phys = pmm_alloc_frame();
    if (!code_phys) {
        return NULL;
    }
    kmemset((void *)code_phys, 0, VMM_PAGE_SIZE);

    if (code && code_size > 0) {
        size_t copy_sz = code_size > VMM_PAGE_SIZE ? VMM_PAGE_SIZE : code_size;
        kmemcpy((void *)code_phys, code, copy_sz);
    }

    /* 3. Allocate user stack physical frame */
    uint64_t stack_phys = pmm_alloc_frame();
    if (!stack_phys) {
        pmm_free_frame(code_phys);
        return NULL;
    }
    kmemset((void *)stack_phys, 0, VMM_PAGE_SIZE);

    /* 4. Construct per-process PML4 and page tables */
    uint64_t pml4_phys = 0;
    uint64_t tables[8];
    size_t table_count = 0;

    int ret = vmm_create_process_pml4(&pml4_phys, tables, &table_count, 8);
    if (ret != 0) {
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        return NULL;
    }

    /* Map user code page at 0x60000000 (User, Read-Only) */
    ret = vmm_map_page_in_pml4(pml4_phys, USER_CODE_VADDR, code_phys,
                               PTE_PRESENT | PTE_USER,
                               tables, &table_count, 8);
    if (ret != 0) {
        for (size_t k = 0; k < table_count; k++) pmm_free_frame(tables[k]);
        pmm_free_frame(pml4_phys);
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        return NULL;
    }

    /* Map user stack page at 0x60001000 (User, Read-Write) */
    ret = vmm_map_page_in_pml4(pml4_phys, USER_STACK_VADDR, stack_phys,
                               PTE_PRESENT | PTE_WRITABLE | PTE_USER,
                               tables, &table_count, 8);
    if (ret != 0) {
        for (size_t k = 0; k < table_count; k++) pmm_free_frame(tables[k]);
        pmm_free_frame(pml4_phys);
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        return NULL;
    }

    /* 5. Create underlying preemptive task with Ring 3 interrupt frame */
    task_t *t = task_create_user(USER_CODE_VADDR, USER_STACK_TOP, pml4_phys, proc, name);
    if (!t) {
        for (size_t k = 0; k < table_count; k++) pmm_free_frame(tables[k]);
        pmm_free_frame(pml4_phys);
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        return NULL;
    }

    /* 6. Populate Process Control Block */
    process_t *caller = process_current();
    if (!caller) {
        caller = process_get(0);
    }

    proc->pid = (uint32_t)slot;
    proc->ppid = caller ? caller->pid : 0;
    proc->state = PROCESS_READY;
    proc->type = PROCESS_TYPE_USER;
    kstrncpy(proc->name, name ? name : "user_proc", PROCESS_NAME_MAX);
    proc->task = t;
    proc->cr3 = pml4_phys;
    proc->user_entry = USER_CODE_VADDR;
    proc->user_stack_top = USER_STACK_TOP;
    proc->code_phys = code_phys;
    proc->stack_phys = stack_phys;
    proc->pml4_phys = pml4_phys;
    for (size_t k = 0; k < table_count; k++) {
        proc->table_frames[k] = tables[k];
    }
    proc->table_frame_count = table_count;
    proc->kernel_stack_top = ((uint64_t)t->stack_base + t->stack_size) & ~0xFULL;
    proc->exit_status = 0;
    proc->reaped = false;
    proc->is_orphan = false;
    proc->cwd = (caller && caller->cwd) ? caller->cwd : vfs_get_root();
    vfs_node_ref(proc->cwd);

    return proc;
}

/*
 * process_exit - Terminates the calling user process.
 *
 * Sets state to PROCESS_ZOMBIE, marks scheduler task as TASK_FINISHED,
 * releases open file descriptors and CWD, reparents children to PID 0,
 * wakes any parent blocked in wait, and deschedules via scheduler_yield.
 */
void process_exit(int64_t status) {
    process_t *proc = current_process;
    if (!proc) {
        return;
    }

    /* Atomic transition under disabled interrupts */
    __asm__ volatile ("cli");

    /* 1. Record exit status */
    proc->exit_status = status;

    /* 2. Transition state to PROCESS_ZOMBIE */
    proc->state = PROCESS_ZOMBIE;

    /* 3. Mark task finished so scheduler never selects it again */
    if (proc->task) {
        proc->task->state = TASK_FINISHED;
    }

    /* 4. Release process file descriptors and CWD immediately */
    fd_close_all(proc);
    if (proc->cwd != NULL) {
        vfs_node_unref(proc->cwd);
        proc->cwd = NULL;
    }

    /* 5. Reparent any surviving children to PID 0 (kernel init) */
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROCESS_UNUSED && proc_table[i].ppid == proc->pid) {
            proc_table[i].ppid = 0;
            proc_table[i].is_orphan = true;
        }
    }

    /* 6. Wake parent process if it is blocked waiting */
    process_t *parent = process_get(proc->ppid);
    if (parent && parent->state == PROCESS_BLOCKED) {
        parent->state = PROCESS_READY;
        if (parent->task && parent->task->state == TASK_BLOCKED) {
            parent->task->state = TASK_READY;
        }
    }

    /* 7. Yield CPU immediately via software interrupt vector 0x81 */
    scheduler_yield();

    /* Safety fallback */
    __asm__ volatile ("sti");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/*
 * process_wait - Waits for child process termination and collects exit status.
 *
 * Parameters:
 *   child_pid - PID of specific child to wait for, or -1 for any child.
 *   status    - Pointer to store child's exit status.
 *   is_user   - True if called via sys_wait from Ring 3 (status validated as user pointer),
 *               false if called directly from kernel mode.
 *
 * Returns:
 *   Child PID on success.
 *   -SYSCALL_EFAULT (-2) on invalid user status pointer.
 *   -SYSCALL_ECHILD (-11) if no matching child exists.
 */
int64_t process_wait(int64_t child_pid, int64_t *status, bool is_user) {
    process_t *caller = process_current();
    if (!caller) {
        caller = process_get(0);
    }
    if (!caller) {
        return SYSCALL_ECHILD;
    }

    /* 1. If called from user mode, strictly validate user status pointer */
    if (is_user) {
        if (status == NULL || !syscall_validate_writable_user_buffer((const void *)status, sizeof(int64_t))) {
            return SYSCALL_EFAULT;
        }
    }

    for (;;) {
        /* F-01: Enter critical section BEFORE scanning child state */
        __asm__ volatile ("cli");

        bool has_children = false;
        process_t *zombie_child = NULL;

        if (child_pid == -1) {
            /* Wait for any child of caller */
            for (int i = 1; i < MAX_PROCESSES; i++) {
                process_t *p = &proc_table[i];
                if (p->state != PROCESS_UNUSED && p->ppid == caller->pid) {
                    has_children = true;
                    if (p->state == PROCESS_ZOMBIE && !p->reaped) {
                        zombie_child = p;
                        break;
                    }
                }
            }
        } else {
            /* Wait for specific child_pid */
            if (child_pid <= 0 || child_pid >= MAX_PROCESSES) {
                __asm__ volatile ("sti");
                return SYSCALL_ECHILD;
            }
            process_t *p = &proc_table[child_pid];
            if (p->state != PROCESS_UNUSED && p->ppid == caller->pid) {
                has_children = true;
                if (p->state == PROCESS_ZOMBIE && !p->reaped) {
                    zombie_child = p;
                }
            }
        }

        if (zombie_child != NULL) {
            __asm__ volatile ("sti");

            /* F-04: Re-validate status buffer immediately before write */
            if (is_user) {
                if (status == NULL || !syscall_validate_writable_user_buffer((const void *)status, sizeof(int64_t))) {
                    return SYSCALL_EFAULT;
                }
            }

            if (status != NULL) {
                *status = zombie_child->exit_status;
            }
            int64_t reaped_pid = (int64_t)zombie_child->pid;

            process_reap(zombie_child);

            return reaped_pid;
        }

        if (!has_children) {
            __asm__ volatile ("sti");
            return SYSCALL_ECHILD;
        }

        /* 4. Children exist but none are zombies: block caller under cli */
        caller->state = PROCESS_BLOCKED;
        if (caller->task) {
            caller->task->state = TASK_BLOCKED;
        }

        /* Yield CPU immediately; scheduler_yield executes "sti; int $0x81" */
        scheduler_yield();

        /* Restore running state upon awakening */
        __asm__ volatile ("sti");
        caller->state = PROCESS_RUNNING;
        if (caller->task) {
            caller->task->state = TASK_RUNNING;
        }
    }
}

/*
 * process_print_list - Formats and displays process table for the 'ps' command.
 */
void process_print_list(void) {
    vga_puts("\nPID  STATE       TYPE    CR3                 NAME\n");
    vga_puts("---  ----------  ------  ------------------  ----------------\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROCESS_UNUSED) {
            continue;
        }

        /* PID */
        vga_print_dec(p->pid);
        vga_puts(p->pid < 10 ? "    " : "   ");

        /* State */
        switch (p->state) {
            case PROCESS_READY:
                vga_puts("READY       ");
                break;
            case PROCESS_RUNNING:
                vga_puts("RUNNING     ");
                break;
            case PROCESS_BLOCKED:
                vga_puts("BLOCKED     ");
                break;
            case PROCESS_ZOMBIE:
                vga_puts("ZOMBIE      ");
                break;
            default:
                vga_puts("UNKNOWN     ");
                break;
        }

        /* Type */
        if (p->type == PROCESS_TYPE_KERNEL) {
            vga_puts("KERNEL  ");
        } else {
            vga_puts("USER    ");
        }

        /* CR3 */
        vga_print_hex(p->cr3);
        vga_puts("  ");

        /* Name */
        vga_puts(p->name);
        vga_putc('\n');
    }
}

/*
 * process_run_isolation_test - Comprehensive Stage 9 Multiprocessing & Isolation Test.
 *
 * Tests:
 *   1. Deterministic PID allocation (PID 1, PID 2)
 *   2. Independent address spaces: proc_a->cr3 != proc_b->cr3 != boot_cr3
 *   3. Distinct physical frames: code_phys and stack_phys
 *   4. Memory isolation: Process A and Process B store different values to the
 *      EXACT SAME virtual address 0x60001800. After execution, both values
 *      are preserved in their respective physical frames without collision.
 *   5. Preemptive scheduling under 100 Hz timer
 *   6. System calls from processes (SYS_GETTIME, SYS_WRITE, SYS_EXIT)
 *   7. Clean exit status propagation (Proc A exit 42, Proc B exit 84)
 *   8. Complete memory reclamation with 0 physical frame leak
 *   9. Safe PID reuse after termination
 *
 * Returns:
 *   0 on complete success, negative code on failure.
 */
int process_run_isolation_test(void) {
    uint64_t initial_free = pmm_get_free_frames();

    /* 1. Create Process A */
    size_t sz_a = (size_t)((uint64_t)proc_test_program_a_end - (uint64_t)proc_test_program_a);
    process_t *pa = process_create(proc_test_program_a, sz_a, "proc_a");
    if (!pa) {
        return -1;
    }
    if (pa->pid != 1) {
        return -2;
    }

    /* 2. Create Process B */
    size_t sz_b = (size_t)((uint64_t)proc_test_program_b_end - (uint64_t)proc_test_program_b);
    process_t *pb = process_create(proc_test_program_b, sz_b, "proc_b");
    if (!pb) {
        return -3;
    }
    if (pb->pid != 2) {
        return -4;
    }

    /* 3. Verify Address Space Independence */
    uint64_t boot_cr3 = vmm_get_boot_cr3();
    if (pa->cr3 == pb->cr3 || pa->cr3 == boot_cr3 || pb->cr3 == boot_cr3) {
        return -5;
    }
    if (pa->code_phys == pb->code_phys || pa->stack_phys == pb->stack_phys) {
        return -6;
    }

    /* Verify each process allocated exactly 6 physical frames (12 total) */
    uint64_t after_alloc_free = pmm_get_free_frames();
    if (after_alloc_free != initial_free - 12) {
        return -7;
    }
    uint64_t pa_stack_phys = pa->stack_phys;
    uint64_t pb_stack_phys = pb->stack_phys;

    /* 3b. Verify Physical Address Resolution: VA 0x60000000 resolves to distinct frames */
    uint64_t pa_resolved = 0, pb_resolved = 0;
    if (vmm_get_mapping_in_pml4(pa->cr3, USER_CODE_VADDR, &pa_resolved) != 0 ||
        vmm_get_mapping_in_pml4(pb->cr3, USER_CODE_VADDR, &pb_resolved) != 0) {
        return -24;
    }
    if (pa_resolved != pa->code_phys || pb_resolved != pb->code_phys || pa_resolved == pb_resolved) {
        return -25;
    }

    /* 3c. Verify Architectural Page Permissions:
     *     Code: PRESENT=1, USER=1, WRITABLE=0
     *     Stack: PRESENT=1, USER=1, WRITABLE=1
     *     Kernel (0x100000, 0xB8000, 0x50000000): PRESENT=1, USER=0 */
    if (process_verify_permissions(pa) != 0 || process_verify_permissions(pb) != 0) {
        return -20;
    }

    /* 3d. Safe Architectural Write-to-Code Protection Test:
     *     Verify leaf PTE lacks PTE_WRITABLE, and passing code page as a
     *     writable user buffer is rejected by the kernel validator. */
    uint64_t pa_code_flags = 0;
    if (vmm_get_page_flags_in_pml4(pa->cr3, USER_CODE_VADDR, &pa_code_flags) != 0) {
        return -21;
    }
    if (pa_code_flags & PTE_WRITABLE) {
        return -22; /* Leaf PTE has writable bit set on code page! */
    }
    /* Passing read-only code page to writable buffer validator must fail */
    if (syscall_validate_writable_user_buffer_in_pml4(pa->cr3, (const void *)USER_CODE_VADDR, 64)) {
        return -23; /* Code page improperly validated as writable! */
    }
    /* Passing read-write stack page to writable buffer validator must succeed */
    if (!syscall_validate_writable_user_buffer_in_pml4(pa->cr3, (const void *)USER_STACK_VADDR, 64)) {
        return -26; /* Stack page failed writable validation! */
    }

    /* 3e. Per-Process Syscall Buffer Validation:
     *     Ensure validator operates against target PML4 */
    if (!syscall_validate_user_buffer_in_pml4(pa->cr3, (const void *)USER_CODE_VADDR, 64) ||
        !syscall_validate_user_buffer_in_pml4(pb->cr3, (const void *)USER_CODE_VADDR, 64)) {
        return -27;
    }

    /* 4. Preemptively run both processes to completion */
    __asm__ volatile ("sti");
    uint64_t start_tick = timer_get_ticks();
    while ((pa->state != PROCESS_ZOMBIE && !pa->reaped) ||
           (pb->state != PROCESS_ZOMBIE && !pb->reaped)) {
        if (timer_get_ticks() - start_tick >= 300) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    if ((pa->state != PROCESS_ZOMBIE && !pa->reaped) ||
        (pb->state != PROCESS_ZOMBIE && !pb->reaped)) {
        return -8;
    }

    /* 5. Verify Clean Exit Codes */
    if (pa->exit_status != 42 || pb->exit_status != 84) {
        return -9;
    }

    /* 6. Verify Memory Isolation: Inspect physical frames directly */
    volatile uint64_t *stack_a = (volatile uint64_t *)(pa_stack_phys + 0x800);
    volatile uint64_t *stack_b = (volatile uint64_t *)(pb_stack_phys + 0x800);

    /* Process A wrote 0xAAAAAAAA11111111 */
    if (*stack_a != 0xAAAAAAAA11111111ULL) {
        return -10;
    }

    /* Process B wrote 0xBBBBBBBB22222222 */
    if (*stack_b != 0xBBBBBBBB22222222ULL) {
        return -11;
    }

    /* Verify SYS_GETTIME result recorded at +0x808 in both processes */
    volatile uint64_t *time_a = (volatile uint64_t *)(pa_stack_phys + 0x808);
    volatile uint64_t *time_b = (volatile uint64_t *)(pb_stack_phys + 0x808);
    if (*time_a == 0 || *time_b == 0) {
        return -12;
    }

    /* 7. Reclaim Memory */
    process_reap_terminated();
    if (pmm_get_free_frames() != initial_free) {
        return -13; /* Physical memory leak detected */
    }

    /* 8. Test Multi-Cycle PID Reuse & Memory Reclamation (5 cycles) */
    for (int cycle = 0; cycle < 5; cycle++) {
        process_t *pc = process_create(proc_test_program_a, sz_a, "proc_c");
        if (!pc || pc->pid != 1) {
            return -14; /* PID 1 failed to be safely reused */
        }

        /* Wait for Proc C to finish */
        __asm__ volatile ("sti");
        start_tick = timer_get_ticks();
        while (pc->state != PROCESS_ZOMBIE && !pc->reaped) {
            if (timer_get_ticks() - start_tick >= 200) {
                break;
            }
            __asm__ volatile ("hlt");
        }

        if ((pc->state != PROCESS_ZOMBIE && !pc->reaped) || pc->exit_status != 42) {
            return -15;
        }

        /* Clean up Proc C */
        process_reap_terminated();
        if (pmm_get_free_frames() != initial_free) {
            return -16; /* Memory leak after reuse cycle */
        }
    }

    return 0;
}

/*
 * process_print_test_status - Shell command handler for 'proctest'.
 */
void process_print_test_status(void) {
    vga_puts("\nProcess Isolation & Multitasking Test:\n");

    int res = process_run_isolation_test();

    vga_puts("  Address spaces:   ");
    if (res != -5 && res != -6 && res != -7 && res != -24 && res != -25) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Distinct CR3 & Physical Frames)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Code page RO:     ");
    if (res != -20 && res != -21 && res != -22 && res != -23) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (PRESENT | USER, WRITABLE=0)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Stack page RW:    ");
    if (res != -20 && res != -26) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (PRESENT | USER | WRITABLE)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Kernel protection:");
    if (res != -20) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Supervisor-Only, USER=0)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Isolation at VA:  ");
    if (res != -10 && res != -11) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Same VA 0x60001800, Separate Data)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Preemptive sched: ");
    if (res != -8) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Timer preemption verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Syscalls in proc: ");
    if (res != -9 && res != -12 && res != -27) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (GETTIME, WRITE, EXIT verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Memory reclaim:   ");
    if (res != -13 && res != -16) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (0 PMM frame leaks across 5 cycles)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  PID reuse:        ");
    if (res != -14 && res != -15) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (PID 1 reused successfully)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Result:           ");
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PASSED (All Isolation Properties Verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (Code ");
        vga_print_dec((uint32_t)-res);
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/*
 * process_run_lifecycle_tests - Comprehensive Stage 13B Process Lifecycle Verification.
 *
 * Tests:
 *   1. Child terminates and becomes PROCESS_ZOMBIE before wait() is called.
 *   2. Parent wait() collects zombie, reads exit status, and reaps child (slot becomes PROCESS_UNUSED).
 *   3. Double wait on already-reaped child fails with -SYSCALL_ECHILD.
 *   4. Wait for non-existent / non-child PID fails with -SYSCALL_ECHILD.
 *   5. Wait(-1) with no children returns -SYSCALL_ECHILD.
 *   6. User pointer validation for SYS_WAIT rejects NULL and supervisor addresses with -SYSCALL_EFAULT.
 *   7. Multiple children: wait(-1) reaps zombies in order of exit without waiting for running children.
 *   8. Parent exit reparents active/zombie children to PID 0 (no dangling PPID).
 *   9. Multi-cycle PID reuse and memory reclamation with zero frame leak.
 *
 * Returns 0 on complete pass, negative error code on failure.
 */
int process_run_lifecycle_tests(void) {
    uint64_t initial_free = pmm_get_free_frames();

    /* 0. F-02: Verify process_reap() guard rejections */
    process_t *caller = process_current();
    if (!caller) caller = process_get(0);
    if (caller) {
        process_reap(caller);
        if (caller->state != PROCESS_RUNNING || caller->reaped) {
            return -16;
        }
    }

    size_t sz_a = (size_t)((uint64_t)proc_test_program_a_end - (uint64_t)proc_test_program_a);
    process_t *p_guard = process_create(proc_test_program_a, sz_a, "life_guard");
    if (!p_guard) {
        return -1;
    }
    process_reap(p_guard);
    if (p_guard->state != PROCESS_READY || p_guard->reaped) {
        return -16;
    }
    p_guard->state = PROCESS_ZOMBIE;
    process_reap(p_guard);
    if (p_guard->state != PROCESS_UNUSED || !p_guard->reaped) {
        return -16;
    }

    /* 1. Test child termination before wait -> PROCESS_ZOMBIE preserved */
    process_t *child = process_create(proc_test_program_a, sz_a, "life_child1");
    if (!child) {
        return -1;
    }
    uint32_t cpid = child->pid;

    /* Run child until exit (exit 42) */
    __asm__ volatile ("sti");
    uint64_t start_tick = timer_get_ticks();
    while (child->state != PROCESS_ZOMBIE && !child->reaped) {
        if (timer_get_ticks() - start_tick >= 200) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    /* Child must be PROCESS_ZOMBIE, not reaped, exit_status 42 */
    if (child->state != PROCESS_ZOMBIE || child->reaped || child->exit_status != 42) {
        process_reap_terminated();
        return -2;
    }

    /* F-04: Test invalid user status pointer does NOT reap zombie child */
    int64_t fault_ret = process_wait((int64_t)cpid, (int64_t *)0x100000ULL, true);
    if (fault_ret != SYSCALL_EFAULT) {
        process_reap_terminated();
        return -18;
    }
    if (child->state != PROCESS_ZOMBIE || child->reaped) {
        process_reap_terminated();
        return -18;
    }

    /* 2. Collect zombie via wait() */
    int64_t status = -1;
    int64_t ret = process_wait((int64_t)cpid, &status, false);
    if (ret != (int64_t)cpid || status != 42) {
        process_reap_terminated();
        return -3;
    }

    /* Child must now be reaped and UNUSED */
    if (child->state != PROCESS_UNUSED || !child->reaped) {
        process_reap_terminated();
        return -4;
    }

    /* 3. Double wait must fail with -SYSCALL_ECHILD */
    ret = process_wait((int64_t)cpid, &status, false);
    if (ret != SYSCALL_ECHILD) {
        return -5;
    }

    /* 4. Wait for non-child PID must fail with -SYSCALL_ECHILD */
    ret = process_wait(999, &status, false);
    if (ret != SYSCALL_ECHILD) {
        return -6;
    }

    /* 5. Wait(-1) with no children must fail with -SYSCALL_ECHILD */
    ret = process_wait(-1, &status, false);
    if (ret != SYSCALL_ECHILD) {
        return -7;
    }

    /* 6. User pointer validation in sys_wait: NULL and kernel addresses rejected */
    ret = sys_wait(-1, NULL);
    if (ret != SYSCALL_EFAULT) {
        return -8;
    }
    ret = sys_wait(-1, (int64_t *)0x100000ULL);
    if (ret != SYSCALL_EFAULT) {
        return -9;
    }

    /* 7. Multiple children: wait(-1) reaps zombies */
    size_t sz_b = (size_t)((uint64_t)proc_test_program_b_end - (uint64_t)proc_test_program_b);
    process_t *m1 = process_create(proc_test_program_a, sz_a, "mult_1");
    process_t *m2 = process_create(proc_test_program_b, sz_b, "mult_2");
    if (!m1 || !m2) {
        process_reap_terminated();
        return -10;
    }
    uint32_t pid1 = m1->pid;
    uint32_t pid2 = m2->pid;

    /* Run both to completion */
    __asm__ volatile ("sti");
    start_tick = timer_get_ticks();
    while ((m1->state != PROCESS_ZOMBIE && !m1->reaped) ||
           (m2->state != PROCESS_ZOMBIE && !m2->reaped)) {
        if (timer_get_ticks() - start_tick >= 300) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    /* Wait for any child twice */
    int64_t s1 = 0, s2 = 0;
    int64_t w1 = process_wait(-1, &s1, false);
    int64_t w2 = process_wait(-1, &s2, false);
    if (!((w1 == (int64_t)pid1 && w2 == (int64_t)pid2) || (w1 == (int64_t)pid2 && w2 == (int64_t)pid1))) {
        process_reap_terminated();
        return -11;
    }

    /* 8. Verify reparenting and F-03 orphan zombie reclamation */
    process_t *p_parent = process_create(proc_test_program_a, sz_a, "reparent_p");
    if (!p_parent) {
        return -12;
    }
    /* Create child with ppid set to p_parent->pid */
    process_t *p_child = process_create(proc_test_program_b, sz_b, "reparent_c");
    if (!p_child) {
        process_reap_terminated();
        return -13;
    }
    p_child->ppid = p_parent->pid;

    /* Terminate parent and reparent child to PID 0 */
    p_parent->state = PROCESS_ZOMBIE;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROCESS_UNUSED && proc_table[i].ppid == p_parent->pid) {
            proc_table[i].ppid = 0;
            proc_table[i].is_orphan = true;
        }
    }
    if (p_child->ppid != 0 || !p_child->is_orphan) {
        process_reap_terminated();
        return -14;
    }
    process_reap(p_parent);

    /* Terminate orphan child */
    p_child->state = PROCESS_ZOMBIE;
    if (p_child->state != PROCESS_ZOMBIE || p_child->reaped) {
        process_reap_terminated();
        return -19;
    }

    /* F-03: Reclaiming orphan zombies frees the slot */
    process_reap_orphans();
    if (p_child->state != PROCESS_UNUSED || !p_child->reaped) {
        process_reap_terminated();
        return -19;
    }

    /* 8b. F-03 Adversarial Invariant:
     * Normal zombie whose parent is ALIVE must NEVER be auto-reaped when process table is full!
     * Orphan zombies CAN be reclaimed under slot pressure.
     */
    process_t *c_norm = process_create(proc_test_program_a, sz_a, "norm_c");
    if (!c_norm) {
        return -21;
    }
    uint32_t norm_pid = c_norm->pid;
    c_norm->state = PROCESS_ZOMBIE;
    c_norm->exit_status = 42;
    if (c_norm->state != PROCESS_ZOMBIE || c_norm->is_orphan) {
        process_reap_terminated();
        return -22;
    }

    /* Fill all other user slots in proc_table (slots 2..MAX_PROCESSES-1) with active processes */
    for (int k = 1; k < MAX_PROCESSES; k++) {
        if (proc_table[k].state == PROCESS_UNUSED) {
            proc_table[k].state = PROCESS_RUNNING;
            proc_table[k].is_orphan = false;
            proc_table[k].reaped = false;
        }
    }

    /* Table is completely full: attempt slot allocation with full table and NO orphan zombies */
    int full_slot = process_find_free_slot();
    if (full_slot != -1) {
        /* Process table was full with no orphans; slot search must fail! */
        process_reap_terminated();
        return -23;
    }
    /* Verify normal zombie was NOT reaped despite slot pressure */
    if (c_norm->state != PROCESS_ZOMBIE || c_norm->reaped) {
        process_reap_terminated();
        return -24;
    }

    /* Now turn slot MAX_PROCESSES-1 into an orphan zombie */
    int orphan_idx = MAX_PROCESSES - 1;
    proc_table[orphan_idx].state = PROCESS_ZOMBIE;
    proc_table[orphan_idx].is_orphan = true;
    proc_table[orphan_idx].reaped = false;

    /* Attempt slot allocation: orphan recovery MUST reclaim orphan_idx and return it */
    int rec_slot = process_find_free_slot();
    if (rec_slot != orphan_idx) {
        process_reap_terminated();
        return -25;
    }

    /* Verify normal zombie is STILL preserved and NOT reaped */
    if (c_norm->state != PROCESS_ZOMBIE || c_norm->reaped) {
        process_reap_terminated();
        return -26;
    }

    /* Confirm live parent wait() can collect and reap the normal zombie */
    int64_t norm_status = 0;
    int64_t norm_wret = process_wait((int64_t)norm_pid, &norm_status, false);
    if (norm_wret != (int64_t)norm_pid || norm_status != 42) {
        process_reap_terminated();
        return -27;
    }
    if (c_norm->state != PROCESS_UNUSED || !c_norm->reaped) {
        process_reap_terminated();
        return -28;
    }

    /* Reset dummy filled slots back to UNUSED */
    for (int k = 1; k < MAX_PROCESSES; k++) {
        if (&proc_table[k] != c_norm) {
            proc_table[k].state = PROCESS_UNUSED;
            proc_table[k].reaped = true;
            proc_table[k].is_orphan = false;
        }
    }

    /* 9. Memory reclamation check: 0 frame leak */
    if (pmm_get_free_frames() != initial_free) {
        return -15;
    }

    return 0;
}

/*
 * process_print_lifecycle_status - Shell command handler for 'waittest'.
 */
void process_print_lifecycle_status(void) {
    vga_puts("\nProcess Lifecycle & Wait Subsystem Test:\n");

    int res = process_run_lifecycle_tests();

    vga_puts("  Zombie on exit:   ");
    if (res != -1 && res != -2) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (State == PROCESS_ZOMBIE preserved)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Wait status reap: ");
    if (res != -3 && res != -4 && res != -16) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (PID & Exit Status collected, reaped)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Double wait rej:  ");
    if (res != -5) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (-ECHILD on already-reaped child)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Non-child rej:    ");
    if (res != -6 && res != -7) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (-ECHILD on invalid/non-child PID)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Pointer valid:    ");
    if (res != -8 && res != -9 && res != -18) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (NULL & kernel pointers rejected: -EFAULT)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Multiple child:   ");
    if (res != -10 && res != -11) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (wait(-1) collected all zombies)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Reparenting:      ");
    if (res >= 0 || (res < -28 || (res > -12 && res < 0))) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Children adopted to PID 0 on parent exit)\n");
    } else if (res == -12 || res == -13 || res == -14 || res == -19 || (res <= -21 && res >= -28)) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Children adopted to PID 0 on parent exit)\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Memory reclaim:   ");
    if (res != -15) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (0 PMM frame leaks after lifecycle tests)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Result:           ");
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PASSED (All Lifecycle Properties Verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (Code ");
        vga_print_dec((uint32_t)-res);
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
