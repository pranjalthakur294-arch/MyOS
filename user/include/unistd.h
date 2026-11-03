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

/* Seek whence modes */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* File type constants for stat */
#define STAT_TYPE_FILE 1
#define STAT_TYPE_DIR  2
#define STAT_TYPE_DEV  3

/* Fixed-width stat structure (40 bytes, 8-byte aligned) */
struct stat {
    uint32_t st_dev;        /* Device / filesystem identifier (1 = RAMFS, 2 = PFS) */
    uint32_t st_ino;        /* Inode number */
    uint32_t st_mode;       /* File type (STAT_TYPE_FILE, STAT_TYPE_DIR, etc.) */
    uint32_t st_nlink;      /* Number of hard links (1) */
    uint64_t st_size;       /* Total file size in bytes */
    uint64_t st_blocks;     /* Number of 512-byte blocks allocated */
    uint32_t st_flags;      /* Permission / metadata flags */
    uint32_t _reserved;     /* Explicit alignment padding */
};

_Static_assert(sizeof(struct stat) == 40, "struct stat must be 40 bytes");

int64_t read(int fd, void *buf, size_t count);
int64_t write(int fd, const void *buf, size_t count);
int64_t seek(int fd, int64_t offset, int whence);
int     close(int fd);
int     open(const char *pathname, int flags);
int     stat(const char *pathname, struct stat *statbuf);
int     mkdir(const char *pathname);
int     unlink(const char *pathname);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int64_t wait(int64_t pid, int64_t *status);
int64_t gettime(void);
void    exit(int status) __attribute__((noreturn));

#endif /* _USER_UNISTD_H */
