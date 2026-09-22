/*
 * user/delayed_exit.c - Freestanding User ELF Program for Stage 13B
 *
 * Demonstrates a user process performing observable computation
 * across multiple scheduler ticks before terminating with status 42.
 */

#include <stdint.h>
#include <stddef.h>

#define SYS_WRITE   1
#define SYS_GETTIME 2

static inline int64_t sys_write(const char *buf, size_t len) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((int64_t)SYS_WRITE), "D"((uint64_t)buf), "S"((uint64_t)len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_gettime(void) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((int64_t)SYS_GETTIME)
        : "rcx", "r11", "memory"
    );
    return ret;
}

volatile uint64_t delay_val = 42;
static const char run_msg[] = "  [delayed_exit] running computation...\n";

int64_t user_main(void) {
    sys_write(run_msg, sizeof(run_msg) - 1);

    /* Perform observable work across multiple timer ticks */
    int64_t start_time = sys_gettime();
    while (sys_gettime() - start_time < 15) {
        volatile uint64_t count = 0;
        for (int i = 0; i < 50000; i++) {
            count += (uint64_t)i;
        }
    }

    return (int64_t)delay_val;
}
