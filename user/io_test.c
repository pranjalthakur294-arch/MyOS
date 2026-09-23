/*
 * user/io_test.c - Exhaustive User-Mode I/O and Standard Stream Test Suite for Stage 14A
 *
 * Verifies:
 *   1. stdout write (fd 1)
 *   2. stderr write (fd 2)
 *   3. zero-length write (returns 0)
 *   4. zero-length read (returns 0)
 *   5. write to stdin rejected (-EACCES)
 *   6. read from stdout rejected (-EACCES)
 *   7. read from stderr rejected (-EACCES)
 *   8. invalid fd write / read (-EBADF)
 *   9. invalid pointer write / read (-EFAULT)
 *   10. read-only code page passed to read (-EFAULT)
 *   11. close(1) leaves fd 2 functional, subsequent write(1) fails (-EBADF)
 *   12. double close(1) fails (-EBADF)
 *   13. Exits with status 0 upon all tests passing
 */

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_READ  4
#define SYS_CLOSE 5

#define EBADF   4
#define EFAULT  2
#define EACCES  6

/* Ensure non-empty .data segment at 0x60001000 for ELF loader */
volatile uint64_t io_data_val = 0xABCD1234EF567890ULL;

static inline int64_t sys_write(int64_t fd, const void *buf, size_t count) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((int64_t)SYS_WRITE), "D"(fd), "S"((uint64_t)buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_read(int64_t fd, void *buf, size_t count) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((int64_t)SYS_READ), "D"(fd), "S"((uint64_t)buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_close(int64_t fd) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((int64_t)SYS_CLOSE), "D"(fd)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static const char msg_ok[] = "io_test: all assertions passed\n";

int64_t user_main(void) {
    (void)io_data_val;

    /* 1. stdout write */
    const char t1[] = "io_test: testing stdout\n";
    if (sys_write(1, t1, sizeof(t1) - 1) != (int64_t)(sizeof(t1) - 1)) return 1;

    /* 2. stderr write */
    const char t2[] = "io_test: testing stderr\n";
    if (sys_write(2, t2, sizeof(t2) - 1) != (int64_t)(sizeof(t2) - 1)) return 2;

    /* 3. zero-length write */
    if (sys_write(1, t1, 0) != 0) return 3;

    /* 4. zero-length read */
    char buf[32];
    if (sys_read(0, buf, 0) != 0) return 4;

    /* 5. write to stdin must fail (-EACCES) */
    if (sys_write(0, t1, 5) != -EACCES) return 5;

    /* 6. read from stdout must fail (-EACCES) */
    if (sys_read(1, buf, 5) != -EACCES) return 6;

    /* 7. read from stderr must fail (-EACCES) */
    if (sys_read(2, buf, 5) != -EACCES) return 7;

    /* 8. invalid fd write / read (-EBADF) */
    if (sys_write(-1, t1, 5) != -EBADF) return 8;
    if (sys_write(99, t1, 5) != -EBADF) return 9;
    if (sys_read(-1, buf, 5) != -EBADF) return 10;
    if (sys_read(99, buf, 5) != -EBADF) return 11;

    /* 9. invalid pointer write / read (-EFAULT) */
    if (sys_write(1, (const void *)0, 10) != -EFAULT) return 12;
    if (sys_read(0, (void *)0, 10) != -EFAULT) return 13;
    if (sys_write(1, (const void *)0x100000, 10) != -EFAULT) return 14; /* kernel address */
    if (sys_read(0, (void *)0x100000, 10) != -EFAULT) return 15; /* kernel address */

    /* 10. read into read-only code page must fail (-EFAULT) */
    if (sys_read(0, (void *)0x60000000, 10) != -EFAULT) return 16;

    /* 11. close(1) then verify write(1) fails with -EBADF while write(2) still succeeds */
    if (sys_close(1) != 0) return 17;
    if (sys_write(1, t1, 5) != -EBADF) return 18;
    const char t_err[] = "io_test: stderr works after close(1)\n";
    if (sys_write(2, t_err, sizeof(t_err) - 1) != (int64_t)(sizeof(t_err) - 1)) return 19;

    /* 12. closing 1 again fails with -EBADF */
    if (sys_close(1) != -EBADF) return 20;

    sys_write(2, msg_ok, sizeof(msg_ok) - 1);
    return 0;
}
