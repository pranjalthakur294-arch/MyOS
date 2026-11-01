/*
 * user/include/string.h - Freestanding Userland String & Memory Operations
 */

#ifndef _USER_STRING_H
#define _USER_STRING_H

#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *s);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
void  *memcpy(void *dest, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);

#endif /* _USER_STRING_H */
