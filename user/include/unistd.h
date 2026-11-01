/*
 * user/include/unistd.h - Standard Userland System Calls & Constants
 */

#ifndef _USER_UNISTD_H
#define _USER_UNISTD_H

#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "dirent.h"

/* Standard streams */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* File access modes */
#define O_RDONLY 0x0001
#define O_WRONLY 0x0002
#define O_RDWR   0x0003

int64_t read(int fd, void *buf, size_t count);
int64_t write(int fd, const void *buf, size_t count);
int     close(int fd);
int     open(const char *pathname, int flags);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int64_t wait(int64_t pid, int64_t *status);
int64_t gettime(void);
void    exit(int status) __attribute__((noreturn));

#endif /* _USER_UNISTD_H */
