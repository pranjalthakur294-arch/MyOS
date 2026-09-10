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
#define SYS_WRITE   1  /* Write to file descriptor or terminal */
#define SYS_GETTIME 2  /* Read current PIT timer ticks */
#define SYS_OPEN    3  /* Open file via VFS path */
#define SYS_READ    4  /* Read from file descriptor */
#define SYS_CLOSE   5  /* Close file descriptor */

/* System Call Return Error Codes (signed 64-bit) */
#define SYSCALL_SUCCESS   0
#define SYSCALL_EINVAL   -1  /* Invalid argument */
#define SYSCALL_EFAULT   -2  /* Bad address / memory protection fault */
#define SYSCALL_ENOSYS   -3  /* Function not implemented / unknown syscall */
#define SYSCALL_EBADF    -4  /* Bad file descriptor */
#define SYSCALL_ENOENT   -5  /* No such file or directory */
#define SYSCALL_EACCES   -6  /* Permission denied */
#define SYSCALL_EISDIR   -7  /* Is a directory */
#define SYSCALL_EMFILE   -8  /* Too many open files */
#define SYSCALL_ENOMEM   -9  /* Out of memory */
#define SYSCALL_ENOTSUP -10  /* Operation not supported */

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
int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int64_t sys_write(int64_t fd_or_buf, const void *buf_or_len, size_t count_or_zero);
int64_t sys_gettime(void);
int64_t sys_open(const char *user_path, uint64_t flags);
int64_t sys_read(int64_t fd, void *user_buf, size_t count);
int64_t sys_close(int64_t fd);
int syscall_copy_user_path(const char *user_path, char *kernel_buf, size_t max_len);
bool syscall_validate_user_buffer(const void *ptr, size_t len);
bool syscall_validate_user_buffer_in_pml4(uint64_t pml4_phys, const void *ptr, size_t len);
bool syscall_validate_writable_user_buffer(const void *ptr, size_t len);
bool syscall_validate_writable_user_buffer_in_pml4(uint64_t pml4_phys, const void *ptr, size_t len);

int syscall_run_test(void);
void syscall_print_status(void);

/* Low-level Assembly Symbols (declared in user.S) */
extern void isr_syscall(void);
extern void syscall_test_program(void);
extern void syscall_test_program_end(void);

/* User-Side Wrappers (executable in Ring 3) */
extern uint64_t user_syscall0(uint64_t number);
extern uint64_t user_syscall1(uint64_t number, uint64_t arg1);
extern uint64_t user_syscall2(uint64_t number, uint64_t arg1, uint64_t arg2);
extern uint64_t user_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif /* SYSCALL_H */
