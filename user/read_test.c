/*
 * user/read_test.c - Freestanding User-Mode Test Executable for Stage 14A
 *
 * Verifies standard stream terminal input:
 *   - Prompts user via stdout (fd 1)
 *   - Reads input line from stdin (fd 0)
 *   - Echoes received input back to stdout (fd 1)
 *   - Exits with status 0
 */

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_READ  4

/* Ensure non-empty .data segment at 0x60001000 for ELF loader */
volatile uint64_t read_data_val = 0xDEADBEEF55667788ULL;

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

static const char prompt_msg[] = "read_test: enter text: ";
static const char echo_prefix[] = "read_test echo: ";

int64_t user_main(void) {
    (void)read_data_val;

    sys_write(1, prompt_msg, sizeof(prompt_msg) - 1);

    char buf[128];
    /* First read up to 4 bytes to verify short read from line buffer */
    int64_t n1 = sys_read(0, buf, 4);
    if (n1 <= 0) {
        return 1;
    }

    /* Second read to consume remainder of submitted line including '\n' */
    int64_t n2 = 0;
    if (buf[n1 - 1] != '\n') {
        n2 = sys_read(0, buf + n1, sizeof(buf) - 1 - (size_t)n1);
        if (n2 < 0) {
            return 2;
        }
    }

    int64_t n = n1 + n2;
    sys_write(1, echo_prefix, sizeof(echo_prefix) - 1);
    sys_write(1, buf, (size_t)n);

    return 0;
}
