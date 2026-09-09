/*
 * user/test_program.c - Freestanding User-Mode ELF Test Program
 *
 * Demonstrates execution of a real ELF64 binary in Ring 3:
 *   - Checks CPL == 3
 *   - Verifies BSS zero-initialization and writes to BSS
 *   - Verifies .data segment loading and writes to .data
 *   - Invokes SYS_GETTIME system call
 *   - Invokes SYS_WRITE system call
 *   - Exits cleanly via SYS_EXIT with status 42
 */

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_GETTIME 2

/* User-space system call wrappers */
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

/* Initialized data in .data segment (proves writable data segment loaded from ELF) */
volatile uint64_t test_data_val = 0xCAFEBABE12345678ULL;

/* Uninitialized data in .bss segment (proves BSS zero-initialization by kernel loader) */
volatile uint64_t test_bss_val;

/* Message to print via SYS_WRITE */
static const char hello_msg[] = "  [ELF Ring 3] Hello from loaded ELF64 executable!\n";

int64_t user_main(void) {
    /* 1. Verify CPL == 3 */
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) {
        return 1; /* Failed: not in Ring 3 */
    }

    /* 2. Verify .bss was zero-initialized by kernel loader */
    if (test_bss_val != 0) {
        return 2; /* Failed: BSS not zeroed */
    }
    test_bss_val = 0xDEADBEEFULL;

    /* 3. Verify .data was properly loaded from ELF image */
    if (test_data_val != 0xCAFEBABE12345678ULL) {
        return 3; /* Failed: .data corrupt */
    }
    test_data_val = 0x5555AAAA5555AAAAULL;

    /* 4. Test SYS_GETTIME syscall from Ring 3 */
    int64_t t1 = sys_gettime();
    if (t1 < 0) {
        return 4; /* Failed: SYS_GETTIME failed */
    }

    /* 5. Test SYS_WRITE syscall from Ring 3 */
    int64_t bytes = sys_write(hello_msg, sizeof(hello_msg) - 1);
    if (bytes != (int64_t)(sizeof(hello_msg) - 1)) {
        return 5; /* Failed: SYS_WRITE failed */
    }

    /* Return success status (passed to SYS_EXIT by _start) */
    return 42;
}
