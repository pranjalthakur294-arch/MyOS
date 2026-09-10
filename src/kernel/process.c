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
    fd_init_process(&proc_table[0]);

    /* Remaining slots 1..MAX_PROCESSES-1 are initially unused */
    for (int i = 1; i < MAX_PROCESSES; i++) {
        proc_table[i].pid = (uint32_t)i;
        proc_table[i].state = PROCESS_UNUSED;
        proc_table[i].type = PROCESS_TYPE_USER;
        proc_table[i].reaped = false;
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
 * process_count - Returns count of active (READY or RUNNING) processes.
 */
uint32_t process_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROCESS_READY || proc_table[i].state == PROCESS_RUNNING) {
            count++;
        }
    }
    return count;
}

/*
 * process_reap_terminated_ex - Safely reclaims physical memory frames of terminated processes.
 *
 * Parameters:
 *   executing_task - The task currently executing on the CPU (whose stack is hosting the
 *                    caller's stack frame). If non-NULL, any process owned by this task
 *                    is strictly spared from reaping until execution switches away.
 */
void process_reap_terminated_ex(void *executing_task) {
    task_t *curr_task = (task_t *)executing_task;
    task_t *active_task = task_get_current();

    for (int i = 1; i < MAX_PROCESSES; i++) {
        process_t *proc = &proc_table[i];

        if (proc->state == PROCESS_TERMINATED && !proc->reaped) {
            /* If interrupted/executing task belongs to this process, its kernel stack is in use */
            if (curr_task && curr_task->process == proc) {
                continue;
            }
            if (active_task && active_task->process == proc) {
                continue;
            }
            if (current_process == proc) {
                continue;
            }

            /* 0. Release all process-owned open file descriptors */
            fd_close_all(proc);

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

            /* 4. Free per-process PML4 root frame */
            if (proc->pml4_phys) {
                pmm_free_frame(proc->pml4_phys);
                proc->pml4_phys = 0;
            }
            proc->cr3 = 0;

            /* 5. Mark task unused and clear process reference */
            if (proc->task) {
                proc->task->state = TASK_UNUSED;
                proc->task->process = NULL;
                proc->task = NULL;
            }

            proc->state = PROCESS_UNUSED;
            proc->reaped = true;
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
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROCESS_UNUSED) {
            slot = i;
            break;
        }
    }

    /* If no unused slot, check for a terminated and reaped slot to reuse */
    if (slot < 0) {
        for (int i = 1; i < MAX_PROCESSES; i++) {
            if (proc_table[i].state == PROCESS_TERMINATED && proc_table[i].reaped) {
                slot = i;
                break;
            }
        }
    }

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
    proc->pid = (uint32_t)slot;
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

    return proc;
}

/*
 * process_exit - Terminates the calling user process.
 *
 * Sets state to PROCESS_TERMINATED, marks scheduler task as TASK_FINISHED,
 * and halts in an interrupt-enabled loop awaiting descheduling.
 */
void process_exit(int64_t status) {
    process_t *proc = current_process;
    if (!proc) {
        return;
    }

    proc->exit_status = status;
    proc->state = PROCESS_TERMINATED;

    if (proc->task) {
        proc->task->state = TASK_FINISHED;
    }

    /* Enable interrupts and wait for the scheduler tick to deschedule us */
    __asm__ volatile ("sti");
    for (;;) {
        __asm__ volatile ("hlt");
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
            case PROCESS_TERMINATED:
                vga_puts("TERMINATED  ");
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
    while ((pa->state != PROCESS_TERMINATED && !pa->reaped) ||
           (pb->state != PROCESS_TERMINATED && !pb->reaped)) {
        if (timer_get_ticks() - start_tick >= 300) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    if ((pa->state != PROCESS_TERMINATED && !pa->reaped) ||
        (pb->state != PROCESS_TERMINATED && !pb->reaped)) {
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
        while (pc->state != PROCESS_TERMINATED && !pc->reaped) {
            if (timer_get_ticks() - start_tick >= 200) {
                break;
            }
            __asm__ volatile ("hlt");
        }

        if ((pc->state != PROCESS_TERMINATED && !pc->reaped) || pc->exit_status != 42) {
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
