#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Maximum number of concurrent tasks supported in Stage 7A.
 * Task 0 is reserved for the initial kernel thread (main).
 */
#define MAX_TASKS          4
#define TASK_STACK_SIZE    4096

/*
 * Lifecycle states of a kernel task.
 */
typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_FINISHED
} task_state_t;

/*
 * Function pointer signature for task entry points.
 */
typedef void (*task_entry_t)(void *arg);

/*
 * Task Control Block (TCB)
 *
 * Represents an independent schedulable unit with its own kernel stack,
 * execution state, and CPU context pointer.
 */
typedef struct task {
    uint32_t id;              /* Task ID / PID (0 = main kernel task) */
    task_state_t state;       /* Current lifecycle state */
    uint64_t rsp;             /* Saved stack pointer (%rsp) */
    void *stack_base;         /* Base pointer of allocated stack buffer */
    size_t stack_size;        /* Size of allocated stack in bytes */
    task_entry_t entry;       /* Task entry function */
    void *arg;                /* Argument passed to entry function */
    char name[16];            /* Human-readable label for debugging */
} task_t;

/*
 * Public Kernel Task API
 */
void task_init(void);
task_t *task_create(task_entry_t entry, void *arg, const char *name);
void task_switch_to(task_t *next);
task_t *task_get_current(void);
void task_bootstrap(void);
void task_exit(void);
void task_run_demo(void);
void task_print_list(void);

/*
 * Low-level assembly context switch routine.
 * Defined in context_switch.S.
 *
 * Parameters:
 *   old_rsp - Pointer to uint64_t where the current task's %rsp will be stored.
 *   new_rsp - The new task's stack pointer to restore into %rsp.
 */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif /* TASK_H */
