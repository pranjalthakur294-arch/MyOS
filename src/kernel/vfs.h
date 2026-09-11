#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Maximum limits for VFS identifiers and paths
 */
#define VFS_NAME_MAX    32   /* Max component name length including null terminator */
#define VFS_PATH_MAX    256  /* Max absolute path length including null terminator */

/*
 * VFS Node Types
 */
typedef enum {
    VFS_NODE_INVALID   = 0,
    VFS_NODE_FILE      = 1,
    VFS_NODE_DIRECTORY = 2,
    VFS_NODE_DEVICE    = 3   /* Reserved for future device driver stages */
} vfs_node_type_t;

/*
 * VFS Error Codes
 */
#define VFS_OK                  0   /* Success */
#define VFS_EOF                 1   /* End of directory reached in readdir */
#define VFS_ERR_INVALID        -1   /* Invalid argument or NULL pointer */
#define VFS_ERR_NOT_FOUND      -2   /* File or path component not found */
#define VFS_ERR_EXISTS         -3   /* Node already exists */
#define VFS_ERR_NOT_DIR        -4   /* Path component is not a directory */
#define VFS_ERR_IS_DIR         -5   /* Operation invalid on a directory */
#define VFS_ERR_NO_MEM         -6   /* Out of kernel heap memory */
#define VFS_ERR_NAME_TOO_LONG  -7   /* Component name exceeds VFS_NAME_MAX - 1 */
#define VFS_ERR_PATH_TOO_LONG  -8   /* Absolute path exceeds VFS_PATH_MAX - 1 */
#define VFS_ERR_NOT_SUPPORTED  -9   /* Operation not supported (e.g. gap writes) */
#define VFS_ERR_IO            -10   /* Generic I/O or internal error */

/* Forward declarations */
typedef struct vfs_node vfs_node_t;
typedef struct vfs_fs vfs_fs_t;
typedef struct vfs_dirent vfs_dirent_t;

/*
 * VFS Directory Entry Structure
 * Represents a single directory entry exposed during directory iteration.
 */
struct vfs_dirent {
    char name[VFS_NAME_MAX];
    vfs_node_type_t type;
    uint64_t size;
};

/*
 * VFS Node Operations Table
 * Filesystems implement these function pointers for their nodes.
 */
typedef struct vfs_node_ops {
    int (*read)(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read);
    int (*write)(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written);
    int (*lookup)(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
    int (*create)(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
    int (*mkdir)(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
    int (*readdir)(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent);
} vfs_node_ops_t;

/*
 * VFS Inode / Node Structure
 * Represents an abstract file, directory, or device in the virtual filesystem.
 */
struct vfs_node {
    char name[VFS_NAME_MAX];
    vfs_node_type_t type;
    uint64_t size;
    uint32_t permissions;
    vfs_node_ops_t *ops;
    vfs_fs_t *fs;
    void *internal_data;
};

/*
 * VFS Filesystem Operations Table
 */
typedef struct vfs_fs_ops {
    int (*unmount)(vfs_fs_t *fs);
} vfs_fs_ops_t;

/*
 * VFS Filesystem Mount Structure
 */
struct vfs_fs {
    const char *name;
    vfs_node_t *root;
    vfs_fs_ops_t *ops;
    void *internal_data;
};

/*
 * Public VFS API
 */
void vfs_init(void);
int vfs_mount_root(vfs_fs_t *fs);
vfs_node_t *vfs_get_root(void);
int vfs_lookup(const char *path, vfs_node_t **out_node);
int vfs_create(const char *path, vfs_node_t **out_node);
int vfs_mkdir(const char *path, vfs_node_t **out_node);
int vfs_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent);
int vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read);
int vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written);
const char *vfs_strerror(int err);
int vfs_run_tests(void);

#endif /* VFS_H */
