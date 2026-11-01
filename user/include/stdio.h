/*
 * user/include/stdio.h - Minimal Freestanding Userland I/O Routines
 */

#ifndef _USER_STDIO_H
#define _USER_STDIO_H

#include <stddef.h>
#include <stdint.h>

void print(const char *s);
void puts(const char *s);
void print_err(const char *s);
void print_dec(int64_t n);
void print_hex(uint64_t n);

#endif /* _USER_STDIO_H */
