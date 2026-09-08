#ifndef USER_H
#define USER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * User Virtual Memory Map
 * Virtual region: 0x60000000 - 0x60002000 (2 x 4 KiB pages)
 * Does not overlap kernel binary, identity map (0-1 GiB), heap, or task stacks.
 */
#define USER_CODE_VADDR   0x60000000ULL
#define USER_STACK_VADDR  0x60001000ULL
#define USER_STACK_TOP    0x60002000ULL

/* Expected magic canary written by user program */
#define USER_CANARY_MAGIC 0x1337BEEFULL

/*
 * User Status Structure located at base of User Stack/Data Page (0x60001000)
 */
typedef struct __attribute__((packed)) {
    volatile uint64_t observed_cpl;  /* CS & 3 recorded by user code */
    volatile uint64_t canary_magic;  /* Canary: 0x1337BEEF */
    volatile uint64_t iterations;    /* Loop counter incremented in Ring 3 */
} user_status_t;

/* Assembly routines declared in user.S */
extern void switch_to_user_mode(uint64_t entry_rip, uint64_t user_rsp);
extern void isr_user_return(void);
extern void user_test_program(void);
extern void user_test_program_end(void);

extern uint64_t user_saved_cs;
extern uint64_t user_saved_rip;
extern uint64_t user_saved_rflags;
extern uint64_t user_saved_rsp;
extern uint64_t user_saved_ss;

/* Public User Mode Subsystem API */
void user_init(void);
int user_run_test(void);
void user_print_status(void);
const user_status_t *user_get_status(void);
uint64_t user_get_code_phys(void);
uint64_t user_get_stack_phys(void);

#endif /* USER_H */
