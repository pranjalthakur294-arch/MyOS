/*
 * syscall.h - System Call Subsystem Interface (int 0x80)
 *
 * Defines the register ABI, system call numbers, error codes, C dispatcher,
 * and user buffer validation interface for MyOS Stage 8B.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * System Call Register ABI:
 *   RAX: System Call Number (in) / Return Value (out)
 *   RDI: Argument 1
 *   RSI: Argument 2
 *
 * System V AMD64 Calling Convention:
 *   When jumping from isr_syscall to syscall_dispatch:
 *   %rdi = RAX (syscall number)
 *   %rsi = RDI (arg 1)
 *   %rdx = RSI (arg 2)
 */

/* System Call Numbers */
#define SYS_EXIT    0  /* Return to kernel caller / terminate user session */
#define SYS_WRITE   1  /* Print characters through kernel VGA/terminal */
#define SYS_GETTIME 2  /* Read current PIT timer ticks */

/* System Call Return Error Codes (signed 64-bit) */
#define SYSCALL_SUCCESS  0
#define SYSCALL_EINVAL  -1  /* Invalid argument */
#define SYSCALL_EFAULT  -2  /* Bad address / memory protection fault */
#define SYSCALL_ENOSYS  -3  /* Function not implemented / unknown syscall */

/* Canary magic recorded upon successful syscall test suite execution */
#define SYSCALL_TEST_MAGIC 0x515CA115ULL

/*
 * Syscall Verification Status Block
 * Located at base of user stack/data page (0x60001000) during syscalltest.
 */
typedef struct __attribute__((packed)) {
    volatile uint64_t observed_cpl;         /* CS & 3 recorded in Ring 3 */
    volatile int64_t  gettime_res1;         /* First SYS_GETTIME tick count */
    volatile int64_t  write_res;            /* SYS_WRITE return value (bytes written) */
    volatile int64_t  gettime_res2;         /* Second SYS_GETTIME tick count */
    volatile int64_t  invalid_syscall_res;  /* Invalid syscall return (-ENOSYS) */
    volatile int64_t  invalid_ptr_res;      /* Invalid pointer return (-EFAULT) */
    volatile int64_t  zero_len_res;         /* Zero-length write return (0) */
    volatile uint64_t test_magic;           /* SYSCALL_TEST_MAGIC */
} syscall_status_t;

/* Kernel-Side Subsystem Functions */
void syscall_init(void);
int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2);
int64_t sys_write(const char *user_buffer, size_t length);
int64_t sys_gettime(void);
int64_t sys_exit(int64_t status);
bool syscall_validate_user_buffer(const void *ptr, size_t len);

int syscall_run_test(void);
void syscall_print_status(void);

/* Low-level Assembly Symbols (declared in user.S) */
extern void isr_syscall(void);
extern void syscall_test_program(void);
extern void syscall_test_program_end(void);

/* User-Side Wrappers (executable in Ring 3) */
extern uint64_t user_syscall0(uint64_t number);
extern uint64_t user_syscall2(uint64_t number, uint64_t arg1, uint64_t arg2);

#endif /* SYSCALL_H */
