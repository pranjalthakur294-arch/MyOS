/*
 * user/include/syscall.h - Userland System Call Numbers & Low-Level Wrappers
 */

#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_GETTIME 2
#define SYS_OPEN    3
#define SYS_READ    4
#define SYS_CLOSE   5
#define SYS_WAIT    6
#define SYS_READDIR 7
#define SYS_CHDIR   8
#define SYS_GETCWD  9
#define SYS_SEEK    10
#define SYS_STAT    11
#define SYS_MKDIR   12
#define SYS_UNLINK  13

/* Syscall error codes (positive values for user errno-like conventions) */
#define SUCCESS  0
#define EINVAL   1  /* Invalid argument */
#define EFAULT   2  /* Bad address */
#define ENOSYS   3  /* Function not implemented */
#define EBADF    4  /* Bad file descriptor */
#define ENOENT   5  /* No such file or directory */
#define EACCES   6  /* Permission denied */
#define EISDIR   7  /* Is a directory */
#define EMFILE   8  /* Too many open files */
#define ENOMEM   9  /* Out of memory */
#define ENOTSUP  10 /* Operation not supported */
#define ECHILD   11 /* No child processes */
#define EBUSY    12 /* Device or resource busy */
#define ENOTDIR  13 /* Not a directory */
#define ERANGE   14 /* Result too large / buffer too small */
#define EEXIST   15 /* File or directory exists */
#define EPERM    16 /* Operation not permitted */
#define EOVERFLOW 17 /* Value too large for defined data type */

int64_t __syscall0(int64_t num);
int64_t __syscall1(int64_t num, int64_t a1);
int64_t __syscall2(int64_t num, int64_t a1, int64_t a2);
int64_t __syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3);

#endif /* _USER_SYSCALL_H */
