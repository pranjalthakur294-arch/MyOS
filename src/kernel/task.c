#include "task.h"
#include "heap.h"
#include "vga.h"
#include "scheduler.h"

/*
 * Static kernel task table for Stage 7A and 7B.
 * Index 0 represents the main kernel task (boot thread).
 */
static task_t task_table[MAX_TASKS];
static task_t *current_task = NULL;

/*
 * Static dedicated kernel task stacks in .bss.
 * Each task slot has an independent 4 KiB buffer aligned to 16 bytes.
 * This guarantees independent, isolated stacks without consuming heap memory.
 */
static uint8_t task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));

/* External symbols from boot.S for the initial kernel stack */
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

/*
 * Simple string copy helper for freestanding kernel
 */
static void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    if (src) {
        while (src[i] != '\0' && i + 1 < max_len) {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
}

/*
 * task_get_current - Returns the currently executing task.
 */
task_t *task_get_current(void) {
    return current_task;
}

/*
 * task_set_current - Sets the currently executing task.
 */
void task_set_current(task_t *t) {
    current_task = t;
}

/*
 * task_get_by_id - Returns pointer to task with specified PID, or NULL if invalid.
 */
task_t *task_get_by_id(uint32_t id) {
    if (id < MAX_TASKS) {
        return &task_table[id];
    }
    return NULL;
}

/*
 * task_get_table - Returns pointer to base of task table.
 */
task_t *task_get_table(void) {
    return task_table;
}

/*
 * task_init - Initializes the kernel task infrastructure.
 *
 * Configures the task table and registers the currently running
 * kernel execution thread (running on the boot stack) as Task 0.
 */
void task_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_table[i].id = (uint32_t)i;
        task_table[i].state = TASK_UNUSED;
        task_table[i].rsp = 0;
        task_table[i].stack_base = (void *)task_stacks[i];
        task_table[i].stack_size = TASK_STACK_SIZE;
        task_table[i].entry = NULL;
        task_table[i].arg = NULL;
        task_table[i].switch_count = 0;
        task_table[i].name[0] = '\0';
        task_table[i].cr3 = 0;
        task_table[i].process = NULL;
    }

    /* Initialize Task 0 (Main / Kernel) */
    task_table[0].id = 0;
    task_table[0].state = TASK_RUNNING;
    task_table[0].rsp = 0; /* Updated on first context switch out */
    task_table[0].stack_base = (void *)stack_bottom;
    task_table[0].stack_size = 16384;
    task_table[0].entry = NULL;
    task_table[0].arg = NULL;
    task_table[0].switch_count = 0;
    task_table[0].cr3 = 0;
    task_table[0].process = NULL;
    str_copy(task_table[0].name, "main", sizeof(task_table[0].name));

    current_task = &task_table[0];

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Kernel task subsystem initialized | [OK] Task stacks allocated\n");
    vga_puts("[OK] Context switching initialized\n");
}

/*
 * task_create - Allocates an independent stack and initializes a new preemptive task.
 *
 * Constructs the 20-quadword (160 bytes) interrupt frame matching what
 * isr_timer expects to pop before iretq.
 *
 * Parameters:
 *   entry - Function pointer to the task entry routine.
 *   arg   - Pointer argument passed to entry upon startup.
 *   name  - Debug label string for the task.
 *
 * Returns:
 *   Pointer to initialized task_t, or NULL on capacity failure.
 */
task_t *task_create(task_entry_t entry, void *arg, const char *name) {
    if (!entry) {
        return NULL;
    }

    /* 1. Locate an unused or finished task slot in sequential order */
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED || task_table[i].state == TASK_FINISHED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL; /* Table full */
    }

    task_t *t = &task_table[slot];
    t->id = (uint32_t)slot;
    t->state = TASK_READY;
    t->stack_base = (void *)task_stacks[slot];
    t->stack_size = TASK_STACK_SIZE;
    t->entry = entry;
    t->arg = arg;
    t->switch_count = 0;
    str_copy(t->name, name ? name : "task", sizeof(t->name));

    /*
     * 2. Construct the initial 20-quadword (160 bytes) interrupt stack frame.
     *
     * Frame layout (from low memory / %rsp to high memory):
     *   sp[0..14]:  15 general-purpose registers (r15..rax, with rdi = arg)
     *   sp[15]:     RIP (task_bootstrap)
     *   sp[16]:     CS (0x08)
     *   sp[17]:     RFLAGS (0x202 = IF=1, bit 1 reserved=1)
     *   sp[18]:     RSP (stack_top - 8, ensuring 16-byte alignment at bootstrap)
     *   sp[19]:     SS (0x10)
     */
    uint64_t stack_top = ((uint64_t)task_stacks[slot] + TASK_STACK_SIZE) & ~0xFULL;
    uint64_t *sp = (uint64_t *)(stack_top - 160);

    sp[19] = 0x10ULL;                         /* SS: kernel data segment */
    sp[18] = stack_top - 8;                   /* RSP: clean stack top */
    sp[17] = 0x202ULL;                        /* RFLAGS: interrupts enabled (IF=1) */
    sp[16] = 0x08ULL;                         /* CS: kernel code segment */
    sp[15] = (uint64_t)task_bootstrap;        /* RIP: entry trampoline */

    /* 15 General Purpose Registers restored by isr_timer */
    sp[14] = 0ULL;                            /* RAX */
    sp[13] = 0ULL;                            /* RCX */
    sp[12] = 0ULL;                            /* RDX */
    sp[11] = 0ULL;                            /* RBX */
    sp[10] = 0ULL;                            /* RBP */
    sp[9]  = 0ULL;                            /* RSI */
    sp[8]  = (uint64_t)arg;                   /* RDI (arg) */
    sp[7]  = 0ULL;                            /* R8 */
    sp[6]  = 0ULL;                            /* R9 */
    sp[5]  = 0ULL;                            /* R10 */
    sp[4]  = 0ULL;                            /* R11 */
    sp[3]  = 0ULL;                            /* R12 */
    sp[2]  = 0ULL;                            /* R13 */
    sp[1]  = 0ULL;                            /* R14 */
    sp[0]  = 0ULL;                            /* R15 */

    t->rsp = (uint64_t)sp;
    return t;
}

/*
 * task_create_coop - Creates a task with a cooperative context_switch frame.
 * Used for Stage 7A manual context switching verification (task_run_demo).
 */
task_t *task_create_coop(task_entry_t entry, void *arg, const char *name) {
    if (!entry) {
        return NULL;
    }

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED || task_table[i].state == TASK_FINISHED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL;
    }

    task_t *t = &task_table[slot];
    t->id = (uint32_t)slot;
    t->state = TASK_READY;
    t->stack_base = (void *)task_stacks[slot];
    t->stack_size = TASK_STACK_SIZE;
    t->entry = entry;
    t->arg = arg;
    t->switch_count = 0;
    str_copy(t->name, name ? name : "task", sizeof(t->name));

    /* 8-quadword cooperative frame for context_switch.S */
    uint64_t stack_top = ((uint64_t)task_stacks[slot] + TASK_STACK_SIZE) & ~0xFULL;
    uint64_t *sp = (uint64_t *)stack_top;

    *(--sp) = 0ULL;                           /* dummy return address */
    *(--sp) = (uint64_t)task_bootstrap;       /* initial RIP for ret */
    *(--sp) = 0x202ULL;                       /* RFLAGS */
    *(--sp) = 0ULL;                           /* RBX */
    *(--sp) = 0ULL;                           /* RBP */
    *(--sp) = 0ULL;                           /* R12 */
    *(--sp) = 0ULL;                           /* R13 */
    *(--sp) = 0ULL;                           /* R14 */
    *(--sp) = 0ULL;                           /* R15 */

    t->rsp = (uint64_t)sp;
    return t;
}

/*
 * task_create_user - Allocates a task slot and stack for a Ring 3 user process.
 *
 * Constructs the initial 20-quadword interrupt frame configured for Ring 3:
 *   CS = 0x23 (User Code, RPL 3)
 *   SS = 0x1B (User Data, RPL 3)
 *   RFLAGS = 0x202 (IF=1)
 *   RIP = user_entry
 *   RSP = user_rsp
 *
 * Parameters:
 *   user_entry - Virtual address of process entry point.
 *   user_rsp   - Virtual address of user stack top.
 *   cr3        - Process PML4 address.
 *   proc       - Pointer to parent process_t structure.
 *   name       - Process name string.
 *
 * Returns:
 *   Pointer to initialized task_t, or NULL if task table is full.
 */
task_t *task_create_user(uint64_t user_entry, uint64_t user_rsp, uint64_t cr3, void *proc, const char *name) {
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED || task_table[i].state == TASK_FINISHED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL;
    }

    task_t *t = &task_table[slot];
    t->id = (uint32_t)slot;
    t->state = TASK_READY;
    t->stack_base = (void *)task_stacks[slot];
    t->stack_size = TASK_STACK_SIZE;
    t->entry = NULL;
    t->arg = NULL;
    t->switch_count = 0;
    t->cr3 = cr3;
    t->process = proc;
    str_copy(t->name, name ? name : "user", sizeof(t->name));

    /* Construct the 20-quadword interrupt stack frame */
    uint64_t stack_top = ((uint64_t)task_stacks[slot] + TASK_STACK_SIZE) & ~0xFULL;
    uint64_t *sp = (uint64_t *)(stack_top - 160);

    sp[19] = 0x1BULL;                         /* SS: User Data Segment (RPL 3) */
    sp[18] = user_rsp;                        /* RSP: User Stack Pointer */
    sp[17] = 0x202ULL;                        /* RFLAGS: interrupts enabled (IF=1) */
    sp[16] = 0x23ULL;                         /* CS: User Code Segment (RPL 3) */
    sp[15] = user_entry;                      /* RIP: User Code Entry Point */

    /* 15 GPRs popped by isr_timer (%r15..%rax) */
    for (int j = 0; j < 15; j++) {
        sp[j] = 0ULL;
    }

    t->rsp = (uint64_t)sp;
    return t;
}

/*
 * task_switch_to - Cooperatively switches CPU execution to target task.
 *
 * Saves the current task's callee registers and %rsp, switches stacks,
 * restores the target task's callee registers, and resumes execution.
 */
void task_switch_to(task_t *next) {
    if (!next || next == current_task) {
        return;
    }

    task_t *prev = current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    current_task = next;

    /* Execute the low-level assembly register & stack pointer switch */
    context_switch(&prev->rsp, next->rsp);
}

/*
 * task_bootstrap - Controlled entry trampoline for newly dispatched tasks.
 *
 * Invoked on the new task's stack via 'ret' from context_switch.
 */
void task_bootstrap(void) {
    task_t *curr = task_get_current();
    if (curr && curr->entry) {
        curr->entry(curr->arg);
    }
    task_exit();
}

/*
 * task_exit - Handles safe termination of the current task.
 *
 * Marks task as FINISHED and yields execution back to another ready task
 * or to the main kernel task (Task 0).
 */
void task_exit(void) {
    task_t *curr = task_get_current();
    if (curr) {
        curr->state = TASK_FINISHED;
    }

    /* Stage 7A cooperative demo handling */
    extern task_t *demo_task_a_ptr;
    extern task_t *demo_task_b_ptr;
    extern task_t *demo_caller_task;

    if (curr == demo_task_a_ptr) {
        if (demo_task_b_ptr && demo_task_b_ptr->state == TASK_READY) {
            task_switch_to(demo_task_b_ptr);
            return;
        }
        if (demo_caller_task) {
            task_switch_to(demo_caller_task);
            return;
        }
    } else if (curr == demo_task_b_ptr) {
        if (demo_task_a_ptr && demo_task_a_ptr->state == TASK_READY) {
            task_switch_to(demo_task_a_ptr);
            return;
        }
        if (demo_caller_task) {
            task_switch_to(demo_caller_task);
            return;
        }
    }

    /*
     * If the preemptive scheduler is active, do not execute a manual cooperative
     * context_switch. Instead, enable interrupts and halt; the next timer tick
     * will automatically deschedule this finished task and resume the next runnable task.
     */
    if (scheduler_is_enabled()) {
        __asm__ volatile ("sti");
        while (1) {
            __asm__ volatile ("hlt");
        }
    }

    /* Stage 7A cooperative context switching fallback */
    task_t *target = NULL;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_READY && &task_table[i] != curr) {
            target = &task_table[i];
            break;
        }
    }

    /* If no other ready task, return to main kernel task */
    if (!target && task_table[0].state != TASK_FINISHED && &task_table[0] != curr) {
        target = &task_table[0];
    }

    if (target) {
        task_switch_to(target);
    } else {
        /* Safety halt loop if no tasks remain */
        while (1) {
            __asm__ volatile ("hlt");
        }
    }
}

/* Dynamic pointers for demo tasks to allow re-runs after scheduler initialization */
task_t *demo_task_a_ptr = NULL;
task_t *demo_task_b_ptr = NULL;
task_t *demo_caller_task = NULL;

/*
 * Task A demonstration routine (Stage 7A cooperative test)
 */
static void demo_task_a(void *arg) {
    (void)arg;
    vga_puts("Task A: start | ");
    task_switch_to(demo_task_b_ptr);
    vga_puts("Task A: resumed | ");
    task_switch_to(demo_task_b_ptr);
    vga_puts("Task A: finished | ");
}

/*
 * Task B demonstration routine (Stage 7A cooperative test)
 */
static void demo_task_b(void *arg) {
    (void)arg;
    vga_puts("Task B: start\n");
    task_switch_to(demo_task_a_ptr);
    vga_puts("Task B: resumed\n");
    task_switch_to(demo_task_a_ptr);
    vga_puts("Task B: finished\n");
}

/*
 * task_run_demo - Executes the manual cooperative context switching demo.
 *
 * Creates Task A and Task B using cooperative context frames, verifies
 * bidirectional context switching, and ensures safe termination.
 */
void task_run_demo(void) {
    bool sched_was_enabled = scheduler_is_enabled();
    if (sched_was_enabled) {
        scheduler_disable();
    }

    demo_caller_task = task_get_current();
    demo_task_a_ptr = task_create_coop(demo_task_a, NULL, "task_a");
    demo_task_b_ptr = task_create_coop(demo_task_b, NULL, "task_b");

    if (!demo_task_a_ptr || !demo_task_b_ptr) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Task creation failed\n");
        if (sched_was_enabled) {
            scheduler_enable();
        }
        return;
    }

    /* Switch to Task A: starts the demonstration sequence */
    task_switch_to(demo_task_a_ptr);

    /*
     * Control returns here once both Task A and Task B have completed
     * their execution sequence and safely terminated via task_exit().
     */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Manual context switch test completed\n");

    demo_task_a_ptr = NULL;
    demo_task_b_ptr = NULL;
    demo_caller_task = NULL;

    if (sched_was_enabled) {
        scheduler_enable();
    }
}

/*
 * task_print_list - Displays active and past tasks for the shell 'tasks' command.
 */
void task_print_list(void) {
    vga_puts("\nTasks:\n  PID  Name       State     Stack Base  Switches\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED) {
            continue;
        }
        vga_puts("  ");
        vga_print_dec(task_table[i].id);
        vga_puts("    ");

        /* Print fixed-width name */
        vga_puts(task_table[i].name);
        size_t len = 0;
        while (task_table[i].name[len] != '\0') len++;
        for (size_t s = len; s < 11; s++) {
            vga_putc(' ');
        }

        /* Print state string */
        switch (task_table[i].state) {
            case TASK_READY:    vga_puts("READY     "); break;
            case TASK_RUNNING:  vga_puts("RUNNING   "); break;
            case TASK_FINISHED: vga_puts("FINISHED  "); break;
            default:            vga_puts("UNKNOWN   "); break;
        }

        if (task_table[i].stack_base) {
            vga_print_hex((uint64_t)task_table[i].stack_base);
        } else {
            vga_puts("(freed)");
        }
        vga_puts("  ");
        vga_print_dec((uint32_t)task_table[i].switch_count);
        vga_putc('\n');
    }
}
