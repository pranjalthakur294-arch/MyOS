/*
 * user/lib/syscall.c - Low-Level Syscall Wrappers for MyOS Userland
 */

#include "syscall.h"
#include "unistd.h"
#include "dirent.h"

int64_t __syscall0(int64_t num) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int64_t __syscall1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int64_t __syscall2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int64_t __syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int64_t read(int fd, void *buf, size_t count) {
    return __syscall3(SYS_READ, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

int64_t write(int fd, const void *buf, size_t count) {
    return __syscall3(SYS_WRITE, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

int close(int fd) {
    return (int)__syscall1(SYS_CLOSE, (int64_t)fd);
}

int open(const char *pathname, int flags) {
    return (int)__syscall2(SYS_OPEN, (int64_t)pathname, (int64_t)flags);
}

int readdir(int fd, struct dirent *dirp) {
    return (int)__syscall2(SYS_READDIR, (int64_t)fd, (int64_t)dirp);
}

int chdir(const char *path) {
    return (int)__syscall1(SYS_CHDIR, (int64_t)path);
}

char *getcwd(char *buf, size_t size) {
    int64_t ret = __syscall2(SYS_GETCWD, (int64_t)buf, (int64_t)size);
    if (ret < 0) {
        return NULL;
    }
    return buf;
}

int64_t wait(int64_t pid, int64_t *status) {
    return __syscall2(SYS_WAIT, pid, (int64_t)status);
}

int64_t gettime(void) {
    return __syscall0(SYS_GETTIME);
}

void exit(int status) {
    __syscall1(SYS_EXIT, (int64_t)status);
    while (1) {
        __asm__ volatile ("pause");
    }
}
