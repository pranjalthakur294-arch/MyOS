/*
 * user/lib/stdio.c - Minimal Freestanding Userland I/O Routines
 */

#include "stdio.h"
#include "unistd.h"
#include "string.h"

void print(const char *s) {
    if (!s) return;
    write(STDOUT_FILENO, s, strlen(s));
}

void puts(const char *s) {
    if (s) {
        write(STDOUT_FILENO, s, strlen(s));
    }
    write(STDOUT_FILENO, "\n", 1);
}

void print_err(const char *s) {
    if (!s) return;
    write(STDERR_FILENO, s, strlen(s));
}

void print_dec(int64_t n) {
    if (n == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }
    uint64_t u;
    if (n < 0) {
        write(STDOUT_FILENO, "-", 1);
        u = (uint64_t)(-(n + 1)) + 1ULL;
    } else {
        u = (uint64_t)n;
    }
    char temp[32];
    int idx = 0;
    while (u > 0) {
        temp[idx++] = (char)('0' + (u % 10));
        u /= 10;
    }
    char buf[32];
    int out = 0;
    for (int i = idx - 1; i >= 0; i--) {
        buf[out++] = temp[i];
    }
    buf[out] = '\0';
    write(STDOUT_FILENO, buf, (size_t)out);
}

void print_hex(uint64_t n) {
    const char hex_chars[] = "0123456789abcdef";
    write(STDOUT_FILENO, "0x", 2);
    if (n == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }
    char temp[20];
    int idx = 0;
    while (n > 0) {
        temp[idx++] = hex_chars[n & 0xF];
        n >>= 4;
    }
    char buf[20];
    int out = 0;
    for (int i = idx - 1; i >= 0; i--) {
        buf[out++] = temp[i];
    }
    buf[out] = '\0';
    write(STDOUT_FILENO, buf, (size_t)out);
}
