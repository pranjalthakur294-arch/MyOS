#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Scheduler Statistics Snapshot Structure
 */
typedef struct {
    uint64_t ticks;            /* Total timer ticks processed by scheduler */
    uint64_t context_switches; /* Total preemptive context switches executed */
    uint32_t current_pid;      /* Task ID / PID of currently running task */
    uint32_t active_tasks;     /* Number of currently active tasks (READY or RUNNING) */
    uint64_t task_a_count;     /* Task A computation loop counter */
    uint64_t task_b_count;     /* Task B computation loop counter */
    uint64_t task_c_count;     /* Task C computation loop counter */
} scheduler_stats_t;

/*
 * Public Scheduler API
 */
void scheduler_init(void);
void scheduler_enable(void);
void scheduler_disable(void);
bool scheduler_is_enabled(void);
uint64_t scheduler_tick(uint64_t current_rsp);
void scheduler_get_stats(scheduler_stats_t *stats);
void scheduler_print_stats(void);
bool scheduler_is_demo_complete(void);
uint64_t scheduler_get_ticks(void);

#endif /* SCHEDULER_H */
