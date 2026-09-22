/*
 * user/exit0.c - Freestanding User ELF Program for Stage 13B
 *
 * Demonstrates a user process exiting cleanly with status 0.
 * In _start (start.S), the return value of user_main is moved into %rdi
 * and SYS_EXIT (syscall 0) is called via int $0x80.
 */

#include <stdint.h>

volatile uint64_t exit_data_val = 0x12345678ULL;

int64_t user_main(void) {
    (void)exit_data_val;
    return 0;
}
