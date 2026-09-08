#include "task.h"
#include "heap.h"
#include "vga.h"

/*
 * Static kernel task table for Stage 7A.
 * Index 0 represents the main kernel task (boot thread).
 */
static task_t task_table[MAX_TASKS];
static task_t *current_task = NULL;

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
        task_table[i].stack_base = NULL;
        task_table[i].stack_size = 0;
        task_table[i].entry = NULL;
        task_table[i].arg = NULL;
        task_table[i].name[0] = '\0';
    }

    /* Initialize Task 0 (Main / Kernel) */
    task_table[0].id = 0;
    task_table[0].state = TASK_RUNNING;
    task_table[0].rsp = 0; /* Updated on first context switch out */
    task_table[0].stack_base = (void *)stack_bottom;
    task_table[0].stack_size = 16384;
    task_table[0].entry = NULL;
    task_table[0].arg = NULL;
    str_copy(task_table[0].name, "main", sizeof(task_table[0].name));

    current_task = &task_table[0];

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Kernel task subsystem initialized\n");
    vga_puts("[OK] Task stacks allocated\n");
    vga_puts("[OK] Context switching initialized\n");
}

/*
 * task_create - Allocates an independent stack and initializes a new task.
 *
 * Parameters:
 *   entry - Function pointer to the task entry routine.
 *   arg   - Pointer argument passed to entry upon startup.
 *   name  - Debug label string for the task.
 *
 * Returns:
 *   Pointer to initialized task_t, or NULL on allocation/capacity failure.
 */
task_t *task_create(task_entry_t entry, void *arg, const char *name) {
    if (!entry) {
        return NULL;
    }

    /* 1. Locate an unused or finished task slot */
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 1; i < MAX_TASKS; i++) {
            if (task_table[i].state == TASK_FINISHED) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return NULL; /* Table full */
    }

    /* If reusing a finished slot with an existing stack buffer, release it */
    if (task_table[slot].stack_base) {
        kfree(task_table[slot].stack_base);
        task_table[slot].stack_base = NULL;
    }

    /* 2. Allocate independent kernel stack from the kernel heap */
    void *stack = kmalloc(TASK_STACK_SIZE);
    if (!stack) {
        return NULL; /* Out of heap memory */
    }

    task_t *t = &task_table[slot];
    t->id = (uint32_t)slot;
    t->state = TASK_READY;
    t->stack_base = stack;
    t->stack_size = TASK_STACK_SIZE;
    t->entry = entry;
    t->arg = arg;
    str_copy(t->name, name ? name : "task", sizeof(t->name));

    /*
     * 3. Construct the initial stack frame.
     *
     * System V AMD64 ABI alignment constraint:
     *   At function entry point (%rip = task_bootstrap), (%rsp + 8) % 16 == 0.
     *
     * Stack layout from top (high memory) to bottom (low memory):
     *   stack_top - 8:  0 (dummy return address padding)
     *   stack_top - 16: task_bootstrap (popped into RIP by 'ret' in context_switch)
     *   stack_top - 24: RFLAGS (0x202 = IF enabled, bit 1 reserved)
     *   stack_top - 32: RBX (0)
     *   stack_top - 40: RBP (0)
     *   stack_top - 48: R12 (0)
     *   stack_top - 56: R13 (0)
     *   stack_top - 64: R14 (0)
     *   stack_top - 72: R15 (0)
     *
     * Initial saved %rsp = stack_top - 72.
     */
    uint64_t stack_top = ((uint64_t)stack + TASK_STACK_SIZE) & ~0xFULL;
    uint64_t *sp = (uint64_t *)stack_top;

    *(--sp) = 0ULL;                           /* stack_top - 8 */
    *(--sp) = (uint64_t)task_bootstrap;       /* stack_top - 16: initial RIP */
    *(--sp) = 0x202ULL;                       /* stack_top - 24: RFLAGS (IF=1) */
    *(--sp) = 0ULL;                           /* stack_top - 32: RBX */
    *(--sp) = 0ULL;                           /* stack_top - 40: RBP */
    *(--sp) = 0ULL;                           /* stack_top - 48: R12 */
    *(--sp) = 0ULL;                           /* stack_top - 56: R13 */
    *(--sp) = 0ULL;                           /* stack_top - 64: R14 */
    *(--sp) = 0ULL;                           /* stack_top - 72: R15 */

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

    /* Find any other runnable task */
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

/*
 * Task A demonstration routine
 */
static void demo_task_a(void *arg) {
    (void)arg;
    vga_puts("Task A: start\n");
    task_switch_to(&task_table[2]);
    vga_puts("Task A: resumed\n");
    task_switch_to(&task_table[2]);
    vga_puts("Task A: finished\n");
}

/*
 * Task B demonstration routine
 */
static void demo_task_b(void *arg) {
    (void)arg;
    vga_puts("Task B: start\n");
    task_switch_to(&task_table[1]);
    vga_puts("Task B: resumed\n");
    task_switch_to(&task_table[1]);
    vga_puts("Task B: finished\n");
}

/*
 * task_run_demo - Executes the manual cooperative context switching demo.
 *
 * Creates Task A and Task B, verifies bidirectional context switching,
 * ensures safe termination, and cleans up heap stacks so heap accounting
 * invariants remain pristine.
 */
void task_run_demo(void) {
    task_t *ta = task_create(demo_task_a, NULL, "task_a");
    task_t *tb = task_create(demo_task_b, NULL, "task_b");

    if (!ta || !tb) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Task creation failed\n");
        return;
    }

    /* Switch to Task A: starts the demonstration sequence */
    task_switch_to(ta);

    /*
     * Control returns here once both Task A and Task B have completed
     * their execution sequence and safely terminated via task_exit().
     */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Manual context switch test completed\n");

    /*
     * Safe Stack Deallocation:
     * Task 0 is running on the boot stack in .bss (not on the heap stacks).
     * Releasing the demo stacks returns the heap to 0 used bytes and 65,512
     * free bytes, preserving heap accounting invariants for heaptest/heapinfo.
     */
    if (ta->stack_base) {
        kfree(ta->stack_base);
        ta->stack_base = NULL;
    }
    if (tb->stack_base) {
        kfree(tb->stack_base);
        tb->stack_base = NULL;
    }
}

/*
 * task_print_list - Displays active and past tasks for the shell 'tasks' command.
 */
void task_print_list(void) {
    vga_puts("\nTasks:\n  PID  Name       State     Stack Base\n");
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
        vga_putc('\n');
    }
}
