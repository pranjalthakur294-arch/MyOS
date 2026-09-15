#ifndef MOUNT_H
#define MOUNT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"
#include "block.h"

/* Bounded Static Limits */
#define MAX_MOUNTS          8
#define MAX_FS_TYPES        8
#define MOUNT_PATH_MAX      VFS_PATH_MAX
#define FS_NAME_MAX         32

/* Generic Mount Error Codes */
#define MOUNT_OK             0
#define MOUNT_ERR_INVALID   -1   /* Bad argument, NULL pointer, malformed path */
#define MOUNT_ERR_NOT_FOUND -2   /* Mount point or filesystem type not found */
#define MOUNT_ERR_EXIST     -3   /* Mount point already mounted or duplicate type */
#define MOUNT_ERR_FULL      -4   /* Mount table or type registry full */
#define MOUNT_ERR_CORRUPT   -5   /* Filesystem corrupt or unformatted */
#define MOUNT_ERR_IO        -6   /* Underlying block device I/O error */
#define MOUNT_ERR_BUSY      -7   /* Mount point busy / root cannot be unmounted */
#define MOUNT_ERR_NOT_DIR   -8   /* Mount target is not a directory */
#define MOUNT_ERR_NODEV     -9   /* Backing device not found or required device missing */

/* Filesystem Capability & Configuration Flags */
#define FS_FLAG_REQUIRES_DEV  (1 << 0)  /* Filesystem requires a block device */
#define FS_FLAG_READONLY      (1 << 1)  /* Mount instance is read-only */

/* Forward declarations */
typedef struct fs_type fs_type_t;
typedef struct fs_instance fs_instance_t;
typedef struct mount_entry mount_entry_t;

/*
 * Filesystem Type Abstraction
 * Describes an implementation of a filesystem (e.g. "ramfs", "pfs").
 */
struct fs_type {
    char name[FS_NAME_MAX];
    uint32_t flags;
    int (*mount)(fs_type_t *type, const char *dev_name, block_device_t *dev, void *data, fs_instance_t **out_instance);
    int (*unmount)(fs_instance_t *instance);
};

/*
 * Filesystem Instance Abstraction
 * Represents a single mounted filesystem instance with dedicated static storage.
 */
struct fs_instance {
    fs_type_t *type;                 /* Backing filesystem type */
    block_device_t *dev;             /* Backing block device (NULL if non-disk) */
    vfs_node_t *root;                /* Root directory node of this filesystem instance */
    void *priv;                      /* Filesystem-private instance data */
    uint32_t flags;                  /* Instance flags */
    vfs_node_t root_node_storage;    /* Dedicated storage for the instance root node */
};

/*
 * Mount Entry (Mount Record)
 * Represents an active attachment of a filesystem instance to a mount point.
 */
struct mount_entry {
    bool active;
    char path[MOUNT_PATH_MAX];       /* Canonical mount point path (e.g. "/", "/disk") */
    char dev_name[BLOCK_NAME_MAX];   /* Device identifier string (e.g. "ata0", or "") */
    fs_type_t *type;                 /* Filesystem type */
    fs_instance_t *instance;         /* Associated filesystem instance */
    uint32_t ref_count;              /* Active reference / busy counter */
    fs_instance_t instance_storage;  /* Dedicated static storage for this slot's instance */
    vfs_node_t *mountpoint_node;     /* VFS directory node in parent filesystem */
};

/*
 * Subsystem Lifecycle API
 */
int mount_init(void);

/*
 * Filesystem Type Registration API
 */
int fs_register_type(fs_type_t *type);
int fs_unregister_type(const char *name);
fs_type_t *fs_find_type(const char *name);
size_t fs_type_count(void);
fs_type_t *fs_get_type_by_index(size_t index);

/*
 * Mount / Unmount Operations API
 */
int vfs_mount(const char *type_name, const char *dev_name, const char *mount_point);
int vfs_unmount(const char *mount_point);

/*
 * Mount Table Inspection & Resolution API
 */
mount_entry_t *mount_find(const char *path);
mount_entry_t *mount_find_by_mountpoint(vfs_node_t *node);
mount_entry_t *mount_find_by_root(vfs_node_t *node);
int mount_check_busy(mount_entry_t *entry);
mount_entry_t *mount_get_by_index(size_t index);
size_t mount_count(void);

/*
 * In-Kernel Verification Suite
 */
int mount_run_tests(void);
int vfs12e_run_tests(void);

#endif /* MOUNT_H */
