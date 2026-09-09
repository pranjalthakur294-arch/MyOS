#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "task.h"

/*
 * Maximum number of processes supported concurrently.
 * PID 0 is reserved for the kernel process context.
 * PIDs 1..MAX_PROCESSES-1 are user processes.
 */
#define MAX_PROCESSES 4
#define PROCESS_NAME_MAX 32

/*
 * Process Execution States
 */
typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_TERMINATED
} process_state_t;

/*
 * Process Privilege Types
 */
typedef enum {
    PROCESS_TYPE_KERNEL = 0,
    PROCESS_TYPE_USER
} process_type_t;

#define MAX_PROCESS_USER_FRAMES  32
#define MAX_PROCESS_TABLE_FRAMES 16

/*
 * Process Control Block (PCB)
 */
typedef struct process {
    uint32_t pid;                           /* Process Identifier */
    process_state_t state;                  /* Current lifecycle state */
    process_type_t type;                    /* Kernel or User process */
    char name[PROCESS_NAME_MAX];            /* Human-readable process name */
    task_t *task;                           /* Underlying scheduler task */
    uint64_t cr3;                           /* Physical address of per-process PML4 root */
    uint64_t user_entry;                    /* User code entry point virtual address */
    uint64_t user_stack_top;                /* User stack top virtual address */
    uint64_t code_phys;                     /* Physical frame backing user code page (Stage 9) */
    uint64_t stack_phys;                    /* Physical frame backing user stack page (Stage 9) */
    uint64_t user_frames[MAX_PROCESS_USER_FRAMES];   /* All user segment/stack physical frames */
    size_t user_frame_count;                /* Number of recorded user physical frames */
    uint64_t pml4_phys;                     /* Physical frame for PML4 table */
    uint64_t table_frames[MAX_PROCESS_TABLE_FRAMES]; /* Intermediate page table frames allocated */
    size_t table_frame_count;               /* Number of intermediate table frames */
    uint64_t kernel_stack_top;              /* Dedicated TSS.rsp0 stack top */
    int64_t exit_status;                    /* Status code passed to SYS_EXIT */
    bool reaped;                            /* True if physical frames have been reclaimed */
    bool is_elf;                            /* True if process was loaded from an ELF executable */
} process_t;

/*
 * Global pointer to currently running user process (NULL if kernel task)
 */
extern process_t *current_process;

/*
 * Process Management Public API
 */
void process_init(void);
process_t *process_create(const void *code, size_t code_size, const char *name);
process_t *process_create_from_elf(const void *image, size_t size, const char *name);
void process_exit(int64_t status);
process_t *process_get(uint32_t pid);
process_t *process_current(void);
uint32_t process_count(void);
void process_reap_terminated(void);
void process_reap_terminated_ex(void *executing_task);
int process_verify_permissions(const process_t *proc);
void process_print_list(void);

/*
 * Stage 9 Isolation & Multiprocessing Verification Test Harness
 */
int process_run_isolation_test(void);
void process_print_test_status(void);

#endif /* PROCESS_H */
