#ifndef PFS_H
#define PFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "block.h"

/* Filesystem Signature & Version */
#define PFS_MAGIC               0x50465331  /* 'PFS1' */
#define PFS_VERSION             1
#define PFS_SECTOR_SIZE         512
#define PFS_DIRECT_BLOCKS       8
#define PFS_MAX_FILE_SIZE       (PFS_DIRECT_BLOCKS * PFS_SECTOR_SIZE)  /* 4096 bytes */
#define PFS_NAME_MAX           31
#define PFS_NO_BLOCK            0xFFFFFFFF
#define PFS_NO_INODE            0
#define PFS_ROOT_INODE          1

/* Inode Types */
#define PFS_INODE_FREE          0
#define PFS_INODE_FILE          1
#define PFS_INODE_DIR           2

/* Directory Entry Types */
#define PFS_ENTRY_FREE          0
#define PFS_ENTRY_FILE          1
#define PFS_ENTRY_DIR           2

/* Error Codes */
#define PFS_OK                   0
#define PFS_ERR_INVALID         -1   /* Invalid argument or NULL pointer */
#define PFS_ERR_NOT_FOUND       -2   /* File or directory not found */
#define PFS_ERR_EXIST           -3   /* Target name already exists */
#define PFS_ERR_NOSPACE         -4   /* Disk full or inode table exhausted */
#define PFS_ERR_CORRUPT         -5   /* Superblock or metadata corrupt */
#define PFS_ERR_IO              -6   /* Underlying block I/O failure */
#define PFS_ERR_NOT_MOUNTED     -7   /* Filesystem is not mounted */
#define PFS_ERR_IS_DIR          -8   /* Attempted file operation on a directory */
#define PFS_ERR_NOT_DIR         -9   /* Expected a directory */
#define PFS_ERR_RANGE           -10  /* Offset out of range or sparse write */

/*
 * On-Disk Superblock (Exactly 512 bytes)
 */
typedef struct {
    uint32_t magic;                 /* PFS_MAGIC ('PFS1') */
    uint32_t version;               /* PFS_VERSION (1) */
    uint32_t sector_size;           /* Must be 512 */
    uint32_t total_sectors;         /* Total sectors on device */

    uint32_t block_bitmap_start;    /* Starting sector of block bitmap */
    uint32_t block_bitmap_sectors;  /* Number of sectors in block bitmap */

    uint32_t inode_bitmap_start;    /* Starting sector of inode bitmap */
    uint32_t inode_bitmap_sectors;  /* Number of sectors in inode bitmap */

    uint32_t inode_table_start;     /* Starting sector of inode table */
    uint32_t inode_table_sectors;   /* Number of sectors in inode table */

    uint32_t data_start;            /* Starting sector of data region */
    uint32_t data_blocks;           /* Total data blocks */

    uint32_t free_blocks;           /* Number of free data blocks */
    uint32_t inode_count;           /* Total inodes in table */
    uint32_t free_inodes;           /* Number of free inodes */

    uint32_t root_inode;            /* Root directory inode number (1) */

    uint8_t  padding[448];          /* Pad to exact 512 bytes */
} pfs_superblock_t;

/*
 * On-Disk Inode (Exactly 64 bytes, 8 per 512-byte sector)
 */
typedef struct {
    uint32_t ino;                           /* 1-based inode number (0 = unused) */
    uint16_t type;                          /* PFS_INODE_FILE, PFS_INODE_DIR */
    uint16_t flags;                         /* Reserved flags */
    uint32_t size;                          /* File size in bytes */
    uint32_t direct[PFS_DIRECT_BLOCKS];     /* Direct block indices (rel to data_start) */
    uint32_t reserved[5];                   /* Pad to 64 bytes */
} pfs_inode_t;

/*
 * On-Disk Directory Entry (Exactly 64 bytes, 8 per 512-byte block)
 */
typedef struct {
    uint32_t inode;                         /* Inode number (0 = free slot) */
    uint8_t  type;                          /* PFS_ENTRY_FILE, PFS_ENTRY_DIR */
    uint8_t  name_len;                      /* Length of file name */
    uint16_t reserved;                      /* Alignment */
    char     name[32];                      /* Null-terminated name (up to 31 chars) */
    uint8_t  padding[24];                   /* Pad to 64 bytes */
} pfs_dirent_t;

/* Compile-time structure size verification */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(pfs_superblock_t) == 512, "pfs_superblock_t must be 512 bytes");
_Static_assert(sizeof(pfs_inode_t) == 64, "pfs_inode_t must be 64 bytes");
_Static_assert(sizeof(pfs_dirent_t) == 64, "pfs_dirent_t must be 64 bytes");
#endif

/* Core Subsystem Management API */
int pfs_init(void);
int pfs_format(block_device_t *dev);
int pfs_mount(block_device_t *dev);
int pfs_unmount(void);
bool pfs_is_mounted(void);
const pfs_superblock_t *pfs_get_superblock(void);
block_device_t *pfs_get_device(void);

/* Directory & File Operations */
int pfs_lookup(uint32_t parent_inode, const char *name, uint32_t *out_inode, uint8_t *out_type);
int pfs_create_file(uint32_t parent_inode, const char *name, uint32_t *out_inode);
int pfs_create_directory(uint32_t parent_inode, const char *name, uint32_t *out_inode);
int pfs_read_file(uint32_t inode_num, uint32_t offset, void *buffer, uint32_t length, uint32_t *bytes_read);
int pfs_write_file(uint32_t inode_num, uint32_t offset, const void *buffer, uint32_t length, uint32_t *bytes_written);

/* In-Kernel Verification Suite */
int pfs_run_tests(void);

#endif /* PFS_H */
