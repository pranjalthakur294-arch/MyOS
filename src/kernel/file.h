/*
 * file.h - File Descriptor & Open-File Table Subsystem (Stage 11B)
 *
 * Implements the three-tier file abstraction:
 *   1. VFS Node (underlying filesystem object)
 *   2. Open-File Object (active instance with independent offset and flags)
 *   3. File Descriptor (per-process integer handle into process FD table)
 */

#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"

/* Maximum number of open file descriptors per process */
#define MAX_PROCESS_FDS 16

/* File access flags */
#define O_RDONLY    0x0001
#define O_WRONLY    0x0002
#define O_RDWR      (O_RDONLY | O_WRONLY)

/* Open file object types */
typedef enum {
    OPEN_FILE_UNUSED = 0,
    OPEN_FILE_VFS,       /* Standard VFS node (file or directory) */
    OPEN_FILE_CONSOLE    /* Character console stream (stdin, stdout, stderr) */
} open_file_type_t;

/*
 * Open-File Object
 * Represents an open file instance with its own cursor offset and access mode.
 */
typedef struct open_file {
    open_file_type_t type;
    vfs_node_t *node;       /* Underlying VFS node (NULL for console streams) */
    uint64_t offset;        /* Current file read/write position */
    uint32_t flags;         /* O_RDONLY, O_WRONLY, O_RDWR */
    uint32_t refcount;      /* Active references (for future fork/dup or sharing) */
    bool is_static;         /* True if statically allocated (console streams) */
} open_file_t;

/*
 * Public File Descriptor API
 */
void file_init(void);
void fd_init_process(void *proc_ptr);
int fd_open(void *proc_ptr, const char *path, uint32_t flags);
int64_t fd_read(void *proc_ptr, int fd, void *buf, size_t count);
int64_t fd_write(void *proc_ptr, int fd, const void *buf, size_t count);
int fd_close(void *proc_ptr, int fd);
int64_t fd_get_size(void *proc_ptr, int fd);
void fd_close_all(void *proc_ptr);
int fd_run_tests(void);

#endif /* FILE_H */
