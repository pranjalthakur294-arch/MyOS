/*
 * user/include/dirent.h - Userland Directory Format & readdir Declaration
 */

#ifndef _USER_DIRENT_H
#define _USER_DIRENT_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX 32

/* Entry types */
#define DT_UNKNOWN 0
#define DT_REG     1   /* Regular file */
#define DT_DIR     2   /* Directory */
#define DT_CHR     3   /* Character device */

/*
 * struct dirent - User-safe directory entry structure.
 * Binary-compatible with kernel vfs_dirent_t, exposing no kernel pointers.
 */
struct dirent {
    char     name[VFS_NAME_MAX];
    uint32_t type;
    uint64_t size;
};

typedef struct dirent dirent_t;

int readdir(int fd, struct dirent *dirp);

#endif /* _USER_DIRENT_H */
