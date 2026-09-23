/*
 * user/write_test.c - Freestanding User-Mode Test Executable for Stage 14A
 *
 * Verifies standard stream output:
 *   - Writes message to stdout (fd 1)
 *   - Writes message to stderr (fd 2)
 *   - Exits with status 0
 */

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT  0
#define SYS_WRITE 1

/* Ensure non-empty .data segment at 0x60001000 for ELF loader */
volatile uint64_t write_data_val = 0xCAFEBABE11223344ULL;

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

static const char stdout_msg[] = "write_test: hello stdout (fd 1)\n";
static const char stderr_msg[] = "write_test: hello stderr (fd 2)\n";

int64_t user_main(void) {
    (void)write_data_val;

    int64_t r1 = sys_write(1, stdout_msg, sizeof(stdout_msg) - 1);
    if (r1 != (int64_t)(sizeof(stdout_msg) - 1)) {
        return 1;
    }

    int64_t r2 = sys_write(2, stderr_msg, sizeof(stderr_msg) - 1);
    if (r2 != (int64_t)(sizeof(stderr_msg) - 1)) {
        return 2;
    }

    return 0;
}
