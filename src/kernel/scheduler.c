#include "scheduler.h"
#include "task.h"
#include "vga.h"
#include "process.h"
#include "vmm.h"
#include "gdt.h"
#include <stddef.h>

/*
 * Scheduler Internal State
 */
static volatile bool scheduler_enabled = false;
static volatile bool scheduler_in_schedule = false;
static volatile uint64_t scheduler_ticks = 0;
static volatile uint64_t context_switches = 0;

/*
 * Demonstration Task State
 */
static volatile uint64_t task_a_count = 0;
static volatile uint64_t task_b_count = 0;
static volatile uint64_t task_c_count = 0;

static volatile int task_a_prints = 0;
static volatile int task_b_prints = 0;
static volatile int task_c_prints = 0;

/*
 * Demo Task A: Increments counter in a computation loop.
 * During the boot demonstration, prints "[A] " twice across timer preemptions.
 */
static void task_a_entry(void *arg) {
    (void)arg;
    for (int r = 0; r < 2; r++) {
        vga_puts("[A] ");
        task_a_prints++;
        uint64_t start_tick = scheduler_ticks;
        while (scheduler_ticks == start_tick) {
            task_a_count++;
        }
    }
    while (1) {
        task_a_count++;
    }
}

/*
 * Demo Task B: Increments counter in a computation loop.
 * During the boot demonstration, prints "[B] " twice across timer preemptions.
 */
static void task_b_entry(void *arg) {
    (void)arg;
    for (int r = 0; r < 2; r++) {
        vga_puts("[B] ");
        task_b_prints++;
        uint64_t start_tick = scheduler_ticks;
        while (scheduler_ticks == start_tick) {
            task_b_count++;
        }
    }
    while (1) {
        task_b_count++;
    }
}

/*
 * Demo Task C: Increments counter in a computation loop.
 * During the boot demonstration, prints "[C] " twice, then exits
 * to demonstrate clean transition to TASK_FINISHED and verify that
 * the scheduler skips finished tasks.
 */
static void task_c_entry(void *arg) {
    (void)arg;
    for (int r = 0; r < 2; r++) {
        vga_puts("[C] ");
        task_c_prints++;
        uint64_t start_tick = scheduler_ticks;
        while (scheduler_ticks == start_tick) {
            task_c_count++;
        }
    }
    task_exit();
}

/*
 * scheduler_get_ticks - Returns total scheduler ticks elapsed.
 */
uint64_t scheduler_get_ticks(void) {
    return scheduler_ticks;
}

/*
 * scheduler_is_demo_complete - Returns true when all demo tasks have printed their markers.
 */
bool scheduler_is_demo_complete(void) {
    return (task_a_prints >= 2 && task_b_prints >= 2 && task_c_prints >= 2);
}

/*
 * scheduler_init - Initializes scheduler state and registers demo tasks.
 */
void scheduler_init(void) {
    scheduler_enabled = false;
    scheduler_in_schedule = false;
    scheduler_ticks = 0;
    context_switches = 0;

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Scheduler initialized | [OK] Timer-driven preemption enabled\n");

    /* Create 3 independent preemptive demo tasks */
    task_t *ta = task_create(task_a_entry, NULL, "task_a");
    task_t *tb = task_create(task_b_entry, NULL, "task_b");
    task_t *tc = task_create(task_c_entry, NULL, "task_c");

    if (ta && tb && tc) {
        vga_puts("[OK] Created Task A | [OK] Created Task B | [OK] Created Task C\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[WARN] Failed to create some scheduler demo tasks\n");
    }

    scheduler_enable();
}

/*
 * scheduler_enable - Enables preemptive timer-driven task switching.
 */
void scheduler_enable(void) {
    scheduler_enabled = true;
}

/*
 * scheduler_disable - Disables preemptive timer-driven task switching.
 */
void scheduler_disable(void) {
    scheduler_enabled = false;
}

/*
 * scheduler_is_enabled - Returns current enable status.
 */
bool scheduler_is_enabled(void) {
    return scheduler_enabled;
}

/*
 * scheduler_tick - Core round-robin scheduling algorithm.
 *
 * Invoked on every timer interrupt (IRQ0 / 100 Hz).
 *
 * Parameters:
 *   current_rsp - Saved stack pointer (%rsp) of interrupted task.
 *
 * Returns:
 *   Stack pointer (%rsp) of task to execute next via iretq.
 */
uint64_t scheduler_tick(uint64_t current_rsp) {
    scheduler_ticks++;

    if (!scheduler_enabled) {
        return current_rsp;
    }

    /* Prevent re-entrancy / nested scheduler calls */
    if (scheduler_in_schedule) {
        return current_rsp;
    }
    scheduler_in_schedule = true;

    task_t *curr = task_get_current();
    if (!curr) {
        scheduler_in_schedule = false;
        return current_rsp;
    }

    /* Save interrupted task's stack pointer */
    curr->rsp = current_rsp;

    /*
     * Round-robin selection:
     * Search forward starting from (curr->id + 1) % MAX_TASKS.
     */
    task_t *table = task_get_table();
    task_t *next = NULL;

    for (int i = 1; i <= MAX_TASKS; i++) {
        uint32_t candidate_id = (curr->id + (uint32_t)i) % MAX_TASKS;
        task_t *candidate = &table[candidate_id];

        /* Skip unused and finished task slots */
        if (candidate->state == TASK_UNUSED || candidate->state == TASK_FINISHED) {
            continue;
        }

        /* Found next runnable task (TASK_READY or TASK_RUNNING) */
        if (candidate->state == TASK_READY || candidate->state == TASK_RUNNING) {
            next = candidate;
            break;
        }
    }

    /* If no other runnable task found, or chosen task is curr, keep running curr */
    if (!next || next == curr) {
        scheduler_in_schedule = false;
        return current_rsp;
    }

    /* Transition states: curr (RUNNING -> READY), next (READY -> RUNNING) */
    if (curr->state == TASK_RUNNING) {
        curr->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    next->switch_count++;
    context_switches++;

    /* Update current task reference */
    task_set_current(next);
    current_process = (process_t *)next->process;

    /* Switch CR3 address space if necessary */
    uint64_t current_cr3 = vmm_read_cr3() & PTE_ADDR_MASK;
    if (next->cr3 != 0 && next->cr3 != current_cr3) {
        vmm_write_cr3(next->cr3);
    } else if (next->cr3 == 0) {
        uint64_t boot_cr3 = vmm_get_boot_cr3();
        if (current_cr3 != boot_cr3) {
            vmm_write_cr3(boot_cr3);
        }
    }

    /* Update TSS.rsp0 if next task is a user process */
    if (next->process) {
        process_t *proc = (process_t *)next->process;
        gdt_set_rsp0(proc->kernel_stack_top);
    }

    /* Deferred reaping: pass interrupted task 'curr' so its active stack is not reaped */
    process_reap_terminated_ex(curr);

    scheduler_in_schedule = false;
    return next->rsp;
}

/*
 * scheduler_get_stats - Gathers a snapshot of current scheduler statistics.
 */
void scheduler_get_stats(scheduler_stats_t *stats) {
    if (!stats) return;

    stats->ticks = scheduler_ticks;
    stats->context_switches = context_switches;

    task_t *curr = task_get_current();
    stats->current_pid = curr ? curr->id : 0;

    task_t *table = task_get_table();
    uint32_t active = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (table[i].state == TASK_READY || table[i].state == TASK_RUNNING) {
            active++;
        }
    }
    stats->active_tasks = active;
    stats->task_a_count = task_a_count;
    stats->task_b_count = task_b_count;
    stats->task_c_count = task_c_count;
}

/*
 * scheduler_print_stats - Formats and displays scheduler statistics for shell.
 */
void scheduler_print_stats(void) {
    scheduler_stats_t stats;
    scheduler_get_stats(&stats);

    vga_puts("\nScheduler:\n");
    vga_puts("  Policy:           Round Robin\n");
    vga_puts("  Timer:            100 Hz\n");
    vga_puts("  Ticks:            ");
    vga_print_dec((uint32_t)stats.ticks);
    vga_puts("\n  Context switches: ");
    vga_print_dec((uint32_t)stats.context_switches);
    vga_puts("\n  Current task:     ");
    vga_print_dec(stats.current_pid);
    task_t *curr = task_get_current();
    if (curr) {
        vga_puts(" (");
        vga_puts(curr->name);
        vga_putc(')');
    }
    vga_puts("\n  Active tasks:     ");
    vga_print_dec(stats.active_tasks);
    vga_puts("\n  Counters:         A=");
    vga_print_dec((uint32_t)stats.task_a_count);
    vga_puts(" B=");
    vga_print_dec((uint32_t)stats.task_b_count);
    vga_puts(" C=");
    vga_print_dec((uint32_t)stats.task_c_count);
    vga_putc('\n');
}
