#include "pfs.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Static Filesystem State (Single Mounted Instance) */
static bool s_pfs_mounted = false;
static block_device_t *s_pfs_dev = NULL;
static pfs_superblock_t s_pfs_sb;

/*
 * Dedicated Static Bounce Buffers in .bss
 * Strict functional isolation completely eliminates buffer aliasing hazards.
 */
static uint8_t s_pfs_sb_buf[PFS_SECTOR_SIZE];       /* Superblock I/O */
static uint8_t s_pfs_inode_buf[PFS_SECTOR_SIZE];    /* Inode table sector I/O */
static uint8_t s_pfs_bitmap_buf[PFS_SECTOR_SIZE];   /* Block & Inode bitmap sector I/O */
static uint8_t s_pfs_dirent_buf[PFS_SECTOR_SIZE];   /* Directory scanning & entry I/O */
static uint8_t s_pfs_data_buf[PFS_SECTOR_SIZE];     /* File payload read/write sector I/O */
static uint8_t s_pfs_zero_buf[PFS_SECTOR_SIZE];     /* Fresh block zeroing sector I/O */

/* Freestanding Memory & String Primitives */
static void kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void kmemset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

static size_t kstrlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static void kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i + 1 < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* Bitmap Bit Manipulation Utilities */
static inline bool bitmap_test(const uint8_t *bitmap, uint32_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1 << (bit % 8));
}

static inline void bitmap_clear(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8] &= (uint8_t)~(1 << (bit % 8));
}

/*
 * pfs_validate_inode - Validates internal consistency of on-disk inode.
 * Checks bounds, direct block list, and file size consistency.
 */
static int pfs_validate_inode(const pfs_inode_t *inode, const pfs_superblock_t *sb) {
    if (inode == NULL || sb == NULL) {
        return PFS_ERR_INVALID;
    }
    if (inode->ino >= sb->inode_count) {
        return PFS_ERR_CORRUPT;
    }
    if (inode->type != PFS_INODE_FREE && inode->type != PFS_INODE_FILE && inode->type != PFS_INODE_DIR) {
        return PFS_ERR_CORRUPT;
    }
    if (inode->type == PFS_INODE_FREE) {
        return PFS_OK;
    }
    if (inode->size > PFS_MAX_FILE_SIZE) {
        return PFS_ERR_CORRUPT;
    }
    uint32_t req_blocks = (inode->size + PFS_SECTOR_SIZE - 1) / PFS_SECTOR_SIZE;
    for (uint32_t b = 0; b < req_blocks; b++) {
        if (inode->direct[b] == PFS_NO_BLOCK || inode->direct[b] >= sb->data_blocks) {
            return PFS_ERR_CORRUPT;
        }
    }
    for (uint32_t b = req_blocks; b < PFS_DIRECT_BLOCKS; b++) {
        if (inode->direct[b] != PFS_NO_BLOCK) {
            return PFS_ERR_CORRUPT;
        }
    }
    return PFS_OK;
}

/*
 * pfs_read_inode - Reads an on-disk inode from the inode table and validates it.
 */
static int pfs_read_inode(uint32_t ino, pfs_inode_t *out_inode) {
    if (ino == 0 || ino >= s_pfs_sb.inode_count || out_inode == NULL) {
        return PFS_ERR_INVALID;
    }
    uint32_t sec_offset = (ino * (uint32_t)sizeof(pfs_inode_t)) / PFS_SECTOR_SIZE;
    uint32_t byte_offset = (ino * (uint32_t)sizeof(pfs_inode_t)) % PFS_SECTOR_SIZE;
    uint32_t phys_sec = s_pfs_sb.inode_table_start + sec_offset;

    int rc = block_read(s_pfs_dev, phys_sec, s_pfs_inode_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    kmemcpy(out_inode, s_pfs_inode_buf + byte_offset, sizeof(pfs_inode_t));
    return pfs_validate_inode(out_inode, &s_pfs_sb);
}

/*
 * pfs_write_inode - Writes an on-disk inode into the inode table.
 */
static int pfs_write_inode(const pfs_inode_t *inode) {
    if (inode == NULL || inode->ino == 0 || inode->ino >= s_pfs_sb.inode_count) {
        return PFS_ERR_INVALID;
    }
    uint32_t sec_offset = (inode->ino * (uint32_t)sizeof(pfs_inode_t)) / PFS_SECTOR_SIZE;
    uint32_t byte_offset = (inode->ino * (uint32_t)sizeof(pfs_inode_t)) % PFS_SECTOR_SIZE;
    uint32_t phys_sec = s_pfs_sb.inode_table_start + sec_offset;

    int rc = block_read(s_pfs_dev, phys_sec, s_pfs_inode_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    kmemcpy(s_pfs_inode_buf + byte_offset, inode, sizeof(pfs_inode_t));
    rc = block_write(s_pfs_dev, phys_sec, s_pfs_inode_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    return PFS_OK;
}

/*
 * pfs_sync_superblock - Writes in-memory superblock state to sector 0.
 */
static int pfs_sync_superblock(void) {
    if (!s_pfs_mounted || s_pfs_dev == NULL) {
        return PFS_ERR_NOT_MOUNTED;
    }
    kmemcpy(s_pfs_sb_buf, &s_pfs_sb, sizeof(pfs_superblock_t));
    int rc = block_write(s_pfs_dev, 0, s_pfs_sb_buf);
    return (rc == BLOCK_OK) ? PFS_OK : PFS_ERR_IO;
}

/*
 * pfs_alloc_block - Allocates a single data block, zeroes its sector on disk,
 * and updates bitmap and superblock metadata.
 */
static int pfs_alloc_block(uint32_t *out_block) {
    if (!s_pfs_mounted || s_pfs_sb.free_blocks == 0 || out_block == NULL) {
        return PFS_ERR_NOSPACE;
    }

    for (uint32_t s = 0; s < s_pfs_sb.block_bitmap_sectors; s++) {
        uint32_t phys_sec = s_pfs_sb.block_bitmap_start + s;
        int rc = block_read(s_pfs_dev, phys_sec, s_pfs_bitmap_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }

        for (uint32_t bit = 0; bit < 4096; bit++) {
            uint32_t global_blk = s * 4096 + bit;
            if (global_blk >= s_pfs_sb.data_blocks) {
                break;
            }
            if (!bitmap_test(s_pfs_bitmap_buf, bit)) {
                /* Mark allocated in bitmap */
                bitmap_set(s_pfs_bitmap_buf, bit);
                rc = block_write(s_pfs_dev, phys_sec, s_pfs_bitmap_buf);
                if (rc != BLOCK_OK) {
                    return PFS_ERR_IO;
                }

                /* Zero the newly allocated data sector (prevent stale disk data exposure) */
                kmemset(s_pfs_zero_buf, 0, PFS_SECTOR_SIZE);
                rc = block_write(s_pfs_dev, s_pfs_sb.data_start + global_blk, s_pfs_zero_buf);
                if (rc != BLOCK_OK) {
                    /* Rollback bitmap allocation */
                    bitmap_clear(s_pfs_bitmap_buf, bit);
                    block_write(s_pfs_dev, phys_sec, s_pfs_bitmap_buf);
                    return PFS_ERR_IO;
                }

                s_pfs_sb.free_blocks--;
                pfs_sync_superblock();
                *out_block = global_blk;
                return PFS_OK;
            }
        }
    }
    return PFS_ERR_NOSPACE;
}

/*
 * pfs_free_block - Deallocates a data block and updates bitmap metadata.
 * Rejects double-free attempts with PFS_ERR_INVALID.
 */
static int pfs_free_block(uint32_t block) {
    if (!s_pfs_mounted || block >= s_pfs_sb.data_blocks) {
        return PFS_ERR_RANGE;
    }

    uint32_t s = block / 4096;
    uint32_t bit = block % 4096;
    uint32_t phys_sec = s_pfs_sb.block_bitmap_start + s;

    int rc = block_read(s_pfs_dev, phys_sec, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    if (!bitmap_test(s_pfs_bitmap_buf, bit)) {
        /* Double free detected */
        return PFS_ERR_INVALID;
    }

    bitmap_clear(s_pfs_bitmap_buf, bit);
    rc = block_write(s_pfs_dev, phys_sec, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    s_pfs_sb.free_blocks++;
    pfs_sync_superblock();
    return PFS_OK;
}

/*
 * pfs_alloc_inode - Allocates an inode from the inode bitmap and initializes it.
 */
static int pfs_alloc_inode(uint16_t type, uint32_t *out_inode) {
    if (!s_pfs_mounted || s_pfs_sb.free_inodes == 0 || out_inode == NULL) {
        return PFS_ERR_NOSPACE;
    }

    int rc = block_read(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    /* Inode 0 reserved sentinel; Inode 1 root directory */
    for (uint32_t ino = 2; ino < s_pfs_sb.inode_count; ino++) {
        if (!bitmap_test(s_pfs_bitmap_buf, ino)) {
            bitmap_set(s_pfs_bitmap_buf, ino);
            rc = block_write(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
            if (rc != BLOCK_OK) {
                return PFS_ERR_IO;
            }

            pfs_inode_t new_ino;
            kmemset(&new_ino, 0, sizeof(pfs_inode_t));
            new_ino.ino = ino;
            new_ino.type = type;
            new_ino.flags = 0;
            new_ino.size = 0;
            for (int k = 0; k < PFS_DIRECT_BLOCKS; k++) {
                new_ino.direct[k] = PFS_NO_BLOCK;
            }

            rc = pfs_write_inode(&new_ino);
            if (rc != PFS_OK) {
                bitmap_clear(s_pfs_bitmap_buf, ino);
                block_write(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
                return rc;
            }

            s_pfs_sb.free_inodes--;
            pfs_sync_superblock();
            *out_inode = ino;
            return PFS_OK;
        }
    }
    return PFS_ERR_NOSPACE;
}

/*
 * pfs_free_inode - Deallocates an inode and any blocks associated with it.
 * Rejects double-free attempts with PFS_ERR_INVALID.
 */
static int pfs_free_inode(uint32_t ino) {
    if (!s_pfs_mounted || ino <= PFS_ROOT_INODE || ino >= s_pfs_sb.inode_count) {
        return PFS_ERR_INVALID;
    }

    /* Read inode bitmap to verify allocated and prevent double-free */
    int rc = block_read(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    if (!bitmap_test(s_pfs_bitmap_buf, ino)) {
        /* Double free detected */
        return PFS_ERR_INVALID;
    }

    pfs_inode_t inode;
    rc = pfs_read_inode(ino, &inode);
    if (rc != PFS_OK) {
        return rc;
    }

    /* Free all allocated direct data blocks */
    for (int k = 0; k < PFS_DIRECT_BLOCKS; k++) {
        if (inode.direct[k] != PFS_NO_BLOCK) {
            pfs_free_block(inode.direct[k]);
            inode.direct[k] = PFS_NO_BLOCK;
        }
    }

    /* Clear inode table entry */
    kmemset(&inode, 0, sizeof(pfs_inode_t));
    inode.ino = 0;
    inode.type = PFS_INODE_FREE;
    pfs_write_inode(&inode);

    /* Clear bit in inode bitmap */
    rc = block_read(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
    if (rc == BLOCK_OK) {
        bitmap_clear(s_pfs_bitmap_buf, ino);
        block_write(s_pfs_dev, s_pfs_sb.inode_bitmap_start, s_pfs_bitmap_buf);
    }

    s_pfs_sb.free_inodes++;
    pfs_sync_superblock();
    return PFS_OK;
}

/*
 * pfs_init - Subsystem initialization (called at boot).
 */
int pfs_init(void) {
    s_pfs_mounted = false;
    s_pfs_dev = NULL;
    kmemset(&s_pfs_sb, 0, sizeof(pfs_superblock_t));
    kmemset(s_pfs_zero_buf, 0, PFS_SECTOR_SIZE);
    return PFS_OK;
}

/*
 * pfs_is_mounted - Returns true if a valid PFS instance is mounted.
 */
bool pfs_is_mounted(void) {
    return s_pfs_mounted;
}

/*
 * pfs_get_superblock - Returns a read-only pointer to the active superblock.
 */
const pfs_superblock_t *pfs_get_superblock(void) {
    return s_pfs_mounted ? &s_pfs_sb : NULL;
}

/*
 * pfs_get_device - Returns the underlying block device.
 */
block_device_t *pfs_get_device(void) {
    return s_pfs_mounted ? s_pfs_dev : NULL;
}

/*
 * pfs_format - Initializes on-disk persistent filesystem structures on dev.
 */
int pfs_format(block_device_t *dev) {
    if (dev == NULL || !dev->registered) {
        return PFS_ERR_INVALID;
    }
    if (dev->sector_size != PFS_SECTOR_SIZE) {
        return PFS_ERR_INVALID;
    }
    if (dev->sector_count < 128) {
        return PFS_ERR_INVALID;
    }

    uint32_t total = dev->sector_count;
    uint32_t inode_cnt = 512;
    if (inode_cnt > total / 8) {
        inode_cnt = 64;
    }

    uint32_t inode_bitmap_secs = 1;
    uint32_t inode_table_secs = (inode_cnt * (uint32_t)sizeof(pfs_inode_t) + PFS_SECTOR_SIZE - 1) / PFS_SECTOR_SIZE;
    uint32_t meta_pre = 1 + inode_bitmap_secs + inode_table_secs;

    if (total <= meta_pre + 2) {
        return PFS_ERR_NOSPACE;
    }
    uint32_t rem = total - meta_pre;
    uint32_t block_bitmap_secs = (rem + 4096) / 4097;
    if (block_bitmap_secs == 0) {
        block_bitmap_secs = 1;
    }
    uint32_t data_blks = rem - block_bitmap_secs;

    uint32_t blk_bm_start = 1;
    uint32_t ino_bm_start = blk_bm_start + block_bitmap_secs;
    uint32_t ino_tb_start = ino_bm_start + inode_bitmap_secs;
    uint32_t data_st = ino_tb_start + inode_table_secs;

    /* Build On-Disk Superblock */
    pfs_superblock_t sb;
    kmemset(&sb, 0, sizeof(pfs_superblock_t));
    sb.magic = PFS_MAGIC;
    sb.version = PFS_VERSION;
    sb.sector_size = PFS_SECTOR_SIZE;
    sb.total_sectors = total;
    sb.block_bitmap_start = blk_bm_start;
    sb.block_bitmap_sectors = block_bitmap_secs;
    sb.inode_bitmap_start = ino_bm_start;
    sb.inode_bitmap_sectors = inode_bitmap_secs;
    sb.inode_table_start = ino_tb_start;
    sb.inode_table_sectors = inode_table_secs;
    sb.data_start = data_st;
    sb.data_blocks = data_blks;
    sb.free_blocks = data_blks - 1; /* Data block 0 allocated to root dir */
    sb.inode_count = inode_cnt;
    sb.free_inodes = inode_cnt - 2; /* Inode 0 reserved, Inode 1 root dir */
    sb.root_inode = PFS_ROOT_INODE;

    /* 1. Format Block Bitmap */
    for (uint32_t s = 0; s < block_bitmap_secs; s++) {
        kmemset(s_pfs_bitmap_buf, 0, PFS_SECTOR_SIZE);
        if (s == 0) {
            bitmap_set(s_pfs_bitmap_buf, 0); /* Block 0 for root dir */
        }
        /* Mark out-of-range trailing bits as allocated */
        for (uint32_t bit = 0; bit < 4096; bit++) {
            uint32_t gbit = s * 4096 + bit;
            if (gbit >= data_blks) {
                bitmap_set(s_pfs_bitmap_buf, bit);
            }
        }
        int rc = block_write(dev, blk_bm_start + s, s_pfs_bitmap_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }
    }

    /* 2. Format Inode Bitmap */
    kmemset(s_pfs_bitmap_buf, 0, PFS_SECTOR_SIZE);
    bitmap_set(s_pfs_bitmap_buf, 0); /* Inode 0 sentinel */
    bitmap_set(s_pfs_bitmap_buf, 1); /* Inode 1 root dir */
    for (uint32_t ino = inode_cnt; ino < 4096; ino++) {
        bitmap_set(s_pfs_bitmap_buf, ino);
    }
    int rc = block_write(dev, ino_bm_start, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    /* 3. Format Inode Table */
    for (uint32_t s = 0; s < inode_table_secs; s++) {
        kmemset(s_pfs_inode_buf, 0, PFS_SECTOR_SIZE);
        if (s == 0) {
            /* Root directory inode at slot 1 */
            pfs_inode_t *root_ino = (pfs_inode_t *)(s_pfs_inode_buf + 1 * sizeof(pfs_inode_t));
            root_ino->ino = PFS_ROOT_INODE;
            root_ino->type = PFS_INODE_DIR;
            root_ino->size = PFS_SECTOR_SIZE;
            root_ino->direct[0] = 0; /* Direct block 0 */
            for (int k = 1; k < PFS_DIRECT_BLOCKS; k++) {
                root_ino->direct[k] = PFS_NO_BLOCK;
            }
        }
        rc = block_write(dev, ino_tb_start + s, s_pfs_inode_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }
    }

    /* 4. Format Root Directory Data Block 0 */
    kmemset(s_pfs_data_buf, 0, PFS_SECTOR_SIZE);
    rc = block_write(dev, data_st + 0, s_pfs_data_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    /* 5. Write Superblock to Sector 0 */
    kmemcpy(s_pfs_sb_buf, &sb, sizeof(pfs_superblock_t));
    rc = block_write(dev, 0, s_pfs_sb_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    /* Re-read and verify superblock */
    rc = block_read(dev, 0, s_pfs_sb_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    pfs_superblock_t *chk = (pfs_superblock_t *)s_pfs_sb_buf;
    if (chk->magic != PFS_MAGIC || chk->version != PFS_VERSION) {
        return PFS_ERR_CORRUPT;
    }

    /* Auto-mount freshly formatted volume */
    kmemcpy(&s_pfs_sb, chk, sizeof(pfs_superblock_t));
    s_pfs_dev = dev;
    s_pfs_mounted = true;
    return PFS_OK;
}

/*
 * pfs_mount - Validates on-disk filesystem metadata and mounts the volume.
 * Does NOT auto-format corrupt or uninitialized media.
 */
int pfs_mount(block_device_t *dev) {
    if (dev == NULL || !dev->registered) {
        return PFS_ERR_INVALID;
    }

    int rc = block_read(dev, 0, s_pfs_sb_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }

    pfs_superblock_t *sb = (pfs_superblock_t *)s_pfs_sb_buf;

    /* 1. Validate magic and version */
    if (sb->magic != PFS_MAGIC || sb->version != PFS_VERSION) {
        return PFS_ERR_CORRUPT;
    }
    /* 2. Validate sector size and total sectors */
    if (sb->sector_size != PFS_SECTOR_SIZE || sb->total_sectors != dev->sector_count) {
        return PFS_ERR_CORRUPT;
    }
    /* 3. Validate metadata region locations and lengths */
    if (sb->block_bitmap_start != 1 || sb->block_bitmap_sectors == 0) {
        return PFS_ERR_CORRUPT;
    }
    if (sb->inode_bitmap_start != sb->block_bitmap_start + sb->block_bitmap_sectors || sb->inode_bitmap_sectors == 0) {
        return PFS_ERR_CORRUPT;
    }
    if (sb->inode_table_start != sb->inode_bitmap_start + sb->inode_bitmap_sectors || sb->inode_table_sectors == 0) {
        return PFS_ERR_CORRUPT;
    }
    if (sb->data_start != sb->inode_table_start + sb->inode_table_sectors || sb->data_blocks == 0) {
        return PFS_ERR_CORRUPT;
    }
    if (sb->data_start + sb->data_blocks != sb->total_sectors) {
        return PFS_ERR_CORRUPT;
    }
    /* 4. Validate free counters */
    if (sb->free_blocks > sb->data_blocks) {
        return PFS_ERR_CORRUPT;
    }
    if (sb->free_inodes > sb->inode_count || sb->inode_count < 2 || sb->free_inodes > sb->inode_count - 2) {
        return PFS_ERR_CORRUPT;
    }
    /* 5. Validate root inode number */
    if (sb->root_inode != PFS_ROOT_INODE || sb->root_inode >= sb->inode_count) {
        return PFS_ERR_CORRUPT;
    }

    /* 6. Validate inode bitmap: Root inode (1) and Sentinel (0) must be marked allocated (1) */
    rc = block_read(dev, sb->inode_bitmap_start, s_pfs_bitmap_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    if (!bitmap_test(s_pfs_bitmap_buf, 0) || !bitmap_test(s_pfs_bitmap_buf, PFS_ROOT_INODE)) {
        return PFS_ERR_CORRUPT;
    }

    /* 7. Validate Root Inode in Inode Table */
    uint32_t sec_offset = (PFS_ROOT_INODE * (uint32_t)sizeof(pfs_inode_t)) / PFS_SECTOR_SIZE;
    uint32_t byte_offset = (PFS_ROOT_INODE * (uint32_t)sizeof(pfs_inode_t)) % PFS_SECTOR_SIZE;
    rc = block_read(dev, sb->inode_table_start + sec_offset, s_pfs_inode_buf);
    if (rc != BLOCK_OK) {
        return PFS_ERR_IO;
    }
    pfs_inode_t *root_ino = (pfs_inode_t *)(s_pfs_inode_buf + byte_offset);
    if (pfs_validate_inode(root_ino, sb) != PFS_OK) {
        return PFS_ERR_CORRUPT;
    }
    if (root_ino->type != PFS_INODE_DIR) {
        return PFS_ERR_CORRUPT;
    }
    if (root_ino->direct[0] == PFS_NO_BLOCK || root_ino->direct[0] >= sb->data_blocks) {
        return PFS_ERR_CORRUPT;
    }

    /* Mount volume cleanly */
    kmemcpy(&s_pfs_sb, sb, sizeof(pfs_superblock_t));
    s_pfs_dev = dev;
    s_pfs_mounted = true;
    return PFS_OK;
}

/*
 * pfs_unmount - Flushes pending superblock metadata and detaches volume.
 */
int pfs_unmount(void) {
    if (!s_pfs_mounted) {
        return PFS_ERR_NOT_MOUNTED;
    }
    pfs_sync_superblock();
    s_pfs_mounted = false;
    s_pfs_dev = NULL;
    return PFS_OK;
}

/*
 * pfs_lookup - Searches parent directory for an entry matching name.
 * Performs directory consistency checks (valid inode bounds, name lengths,
 * null termination, and duplicate entry detection).
 */
int pfs_lookup(uint32_t parent_inode, const char *name, uint32_t *out_inode, uint8_t *out_type) {
    if (!s_pfs_mounted) {
        return PFS_ERR_NOT_MOUNTED;
    }
    if (name == NULL || *name == '\0' || kstrlen(name) > PFS_NAME_MAX) {
        return PFS_ERR_INVALID;
    }

    pfs_inode_t parent;
    int rc = pfs_read_inode(parent_inode, &parent);
    if (rc != PFS_OK) {
        return rc;
    }
    if (parent.type != PFS_INODE_DIR) {
        return PFS_ERR_NOT_DIR;
    }

    uint32_t found_inode = 0;
    uint8_t found_type = 0;
    bool found = false;
    uint32_t entries_per_sec = PFS_SECTOR_SIZE / (uint32_t)sizeof(pfs_dirent_t);

    for (int b = 0; b < PFS_DIRECT_BLOCKS; b++) {
        if (parent.direct[b] == PFS_NO_BLOCK) {
            continue;
        }
        if (parent.direct[b] >= s_pfs_sb.data_blocks) {
            return PFS_ERR_CORRUPT;
        }
        uint32_t phys_sec = s_pfs_sb.data_start + parent.direct[b];
        rc = block_read(s_pfs_dev, phys_sec, s_pfs_dirent_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }

        for (uint32_t j = 0; j < entries_per_sec; j++) {
            pfs_dirent_t *de = (pfs_dirent_t *)(s_pfs_dirent_buf + j * sizeof(pfs_dirent_t));
            if (de->inode == 0) {
                continue;
            }

            /* Validate directory entry integrity */
            if (de->inode >= s_pfs_sb.inode_count) {
                return PFS_ERR_CORRUPT;
            }
            if (de->name_len > PFS_NAME_MAX || de->name_len == 0) {
                return PFS_ERR_CORRUPT;
            }
            if (de->name[sizeof(de->name) - 1] != '\0') {
                return PFS_ERR_CORRUPT;
            }
            if (kstrlen(de->name) != de->name_len) {
                return PFS_ERR_CORRUPT;
            }
            if (de->type != PFS_ENTRY_FILE && de->type != PFS_ENTRY_DIR) {
                return PFS_ERR_CORRUPT;
            }

            if (kstrcmp(de->name, name) == 0) {
                if (found) {
                    /* Duplicate directory entry detected on disk! Corrupted directory */
                    return PFS_ERR_CORRUPT;
                }
                found = true;
                found_inode = de->inode;
                found_type = de->type;
            }
        }
    }

    if (found) {
        if (out_inode != NULL) *out_inode = found_inode;
        if (out_type != NULL) *out_type = found_type;
        return PFS_OK;
    }
    return PFS_ERR_NOT_FOUND;
}

/*
 * pfs_create_entry - Internal helper to create a file or directory entry.
 * Implements strict allocation rollback on any failure.
 */
static int pfs_create_entry(uint32_t parent_inode, const char *name, uint16_t type, uint32_t *out_inode) {
    if (!s_pfs_mounted) {
        return PFS_ERR_NOT_MOUNTED;
    }
    if (name == NULL || *name == '\0' || kstrlen(name) > PFS_NAME_MAX) {
        return PFS_ERR_INVALID;
    }
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/') {
            return PFS_ERR_INVALID;
        }
    }

    pfs_inode_t parent;
    int rc = pfs_read_inode(parent_inode, &parent);
    if (rc != PFS_OK) {
        return rc;
    }
    if (parent.type != PFS_INODE_DIR) {
        return PFS_ERR_NOT_DIR;
    }

    /* Duplicate entry check */
    uint32_t dummy_ino;
    if (pfs_lookup(parent_inode, name, &dummy_ino, NULL) == PFS_OK) {
        return PFS_ERR_EXIST;
    }

    /* 1. Allocate New Inode */
    uint32_t new_ino = 0;
    rc = pfs_alloc_inode(type, &new_ino);
    if (rc != PFS_OK) {
        return rc;
    }

    /* 2. If directory, allocate initial data block */
    uint32_t new_dir_blk = PFS_NO_BLOCK;
    if (type == PFS_INODE_DIR) {
        rc = pfs_alloc_block(&new_dir_blk);
        if (rc != PFS_OK) {
            pfs_free_inode(new_ino); /* ROLLBACK */
            return rc;
        }
        pfs_inode_t dir_ino_obj;
        pfs_read_inode(new_ino, &dir_ino_obj);
        dir_ino_obj.size = PFS_SECTOR_SIZE;
        dir_ino_obj.direct[0] = new_dir_blk;
        pfs_write_inode(&dir_ino_obj);
    }

    /* 3. Locate empty dirent slot in parent directory */
    int target_blk_idx = -1;
    int target_slot_idx = -1;
    uint32_t entries_per_sec = PFS_SECTOR_SIZE / (uint32_t)sizeof(pfs_dirent_t);

    for (int b = 0; b < PFS_DIRECT_BLOCKS; b++) {
        if (parent.direct[b] == PFS_NO_BLOCK) {
            continue;
        }
        rc = block_read(s_pfs_dev, s_pfs_sb.data_start + parent.direct[b], s_pfs_dirent_buf);
        if (rc != BLOCK_OK) {
            if (new_dir_blk != PFS_NO_BLOCK) pfs_free_block(new_dir_blk);
            pfs_free_inode(new_ino);
            return PFS_ERR_IO;
        }
        for (uint32_t j = 0; j < entries_per_sec; j++) {
            pfs_dirent_t *de = (pfs_dirent_t *)(s_pfs_dirent_buf + j * sizeof(pfs_dirent_t));
            if (de->inode == 0) {
                target_blk_idx = b;
                target_slot_idx = (int)j;
                break;
            }
        }
        if (target_blk_idx != -1) {
            break;
        }
    }

    /* 4. If all current parent blocks are full, allocate a new block for parent */
    if (target_blk_idx == -1) {
        int empty_direct = -1;
        for (int b = 0; b < PFS_DIRECT_BLOCKS; b++) {
            if (parent.direct[b] == PFS_NO_BLOCK) {
                empty_direct = b;
                break;
            }
        }
        if (empty_direct == -1) {
            /* Parent directory is full (64 entries limit reached) */
            if (new_dir_blk != PFS_NO_BLOCK) pfs_free_block(new_dir_blk);
            pfs_free_inode(new_ino);
            return PFS_ERR_NOSPACE;
        }
        uint32_t new_parent_blk = 0;
        rc = pfs_alloc_block(&new_parent_blk);
        if (rc != PFS_OK) {
            if (new_dir_blk != PFS_NO_BLOCK) pfs_free_block(new_dir_blk);
            pfs_free_inode(new_ino);
            return rc;
        }
        parent.direct[empty_direct] = new_parent_blk;
        parent.size += PFS_SECTOR_SIZE;
        pfs_write_inode(&parent);
        target_blk_idx = empty_direct;
        target_slot_idx = 0;
    }

    /* 5. Write Dirent into parent block */
    rc = block_read(s_pfs_dev, s_pfs_sb.data_start + parent.direct[target_blk_idx], s_pfs_dirent_buf);
    if (rc != BLOCK_OK) {
        if (new_dir_blk != PFS_NO_BLOCK) pfs_free_block(new_dir_blk);
        pfs_free_inode(new_ino);
        return PFS_ERR_IO;
    }

    pfs_dirent_t *de = (pfs_dirent_t *)(s_pfs_dirent_buf + target_slot_idx * sizeof(pfs_dirent_t));
    kmemset(de, 0, sizeof(pfs_dirent_t));
    de->inode = new_ino;
    de->type = (type == PFS_INODE_DIR) ? PFS_ENTRY_DIR : PFS_ENTRY_FILE;
    de->name_len = (uint8_t)kstrlen(name);
    kstrncpy(de->name, name, sizeof(de->name));

    rc = block_write(s_pfs_dev, s_pfs_sb.data_start + parent.direct[target_blk_idx], s_pfs_dirent_buf);
    if (rc != BLOCK_OK) {
        if (new_dir_blk != PFS_NO_BLOCK) pfs_free_block(new_dir_blk);
        pfs_free_inode(new_ino);
        return PFS_ERR_IO;
    }

    if (out_inode != NULL) {
        *out_inode = new_ino;
    }
    return PFS_OK;
}

/*
 * pfs_create_file - Creates a regular file in parent directory.
 */
int pfs_create_file(uint32_t parent_inode, const char *name, uint32_t *out_inode) {
    return pfs_create_entry(parent_inode, name, PFS_INODE_FILE, out_inode);
}

/*
 * pfs_create_directory - Creates a subdirectory in parent directory.
 */
int pfs_create_directory(uint32_t parent_inode, const char *name, uint32_t *out_inode) {
    return pfs_create_entry(parent_inode, name, PFS_INODE_DIR, out_inode);
}

/*
 * pfs_read_file - Reads data from a regular file.
 */
int pfs_read_file(uint32_t inode_num, uint32_t offset, void *buffer, uint32_t length, uint32_t *bytes_read) {
    if (!s_pfs_mounted) {
        return PFS_ERR_NOT_MOUNTED;
    }
    if (buffer == NULL || bytes_read == NULL) {
        return PFS_ERR_INVALID;
    }

    pfs_inode_t inode;
    int rc = pfs_read_inode(inode_num, &inode);
    if (rc != PFS_OK) {
        return rc;
    }
    if (inode.type != PFS_INODE_FILE) {
        return PFS_ERR_IS_DIR;
    }

    if (offset >= inode.size || length == 0) {
        *bytes_read = 0;
        return PFS_OK;
    }

    uint32_t avail = inode.size - offset;
    uint32_t to_read = (length < avail) ? length : avail;
    uint32_t done = 0;

    while (done < to_read) {
        uint32_t cur_pos = offset + done;
        uint32_t blk_idx = cur_pos / PFS_SECTOR_SIZE;
        uint32_t blk_off = cur_pos % PFS_SECTOR_SIZE;
        uint32_t chunk = PFS_SECTOR_SIZE - blk_off;
        if (chunk > to_read - done) {
            chunk = to_read - done;
        }

        if (blk_idx >= PFS_DIRECT_BLOCKS || inode.direct[blk_idx] == PFS_NO_BLOCK) {
            return PFS_ERR_CORRUPT;
        }

        rc = block_read(s_pfs_dev, s_pfs_sb.data_start + inode.direct[blk_idx], s_pfs_data_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }

        kmemcpy((uint8_t *)buffer + done, s_pfs_data_buf + blk_off, chunk);
        done += chunk;
    }

    *bytes_read = done;
    return PFS_OK;
}

/*
 * pfs_write_file - Writes data to a regular file, allocating blocks as needed.
 * Implements strict allocation rollback if any block allocation fails.
 */
int pfs_write_file(uint32_t inode_num, uint32_t offset, const void *buffer, uint32_t length, uint32_t *bytes_written) {
    if (!s_pfs_mounted) {
        return PFS_ERR_NOT_MOUNTED;
    }
    if (buffer == NULL || bytes_written == NULL) {
        return PFS_ERR_INVALID;
    }

    pfs_inode_t inode;
    int rc = pfs_read_inode(inode_num, &inode);
    if (rc != PFS_OK) {
        return rc;
    }
    if (inode.type != PFS_INODE_FILE) {
        return PFS_ERR_IS_DIR;
    }

    /* Sparse writes forbidden */
    if (offset > inode.size) {
        return PFS_ERR_RANGE;
    }

    if (length == 0) {
        *bytes_written = 0;
        return PFS_OK;
    }

    /* Overflow check before addition */
    if (offset > PFS_MAX_FILE_SIZE || length > PFS_MAX_FILE_SIZE) {
        return PFS_ERR_RANGE;
    }
    uint32_t end_pos = offset + length;
    if (end_pos > PFS_MAX_FILE_SIZE) {
        return PFS_ERR_RANGE;
    }

    uint32_t req_blocks = (end_pos + PFS_SECTOR_SIZE - 1) / PFS_SECTOR_SIZE;
    uint32_t curr_blocks = 0;
    for (int k = 0; k < PFS_DIRECT_BLOCKS; k++) {
        if (inode.direct[k] != PFS_NO_BLOCK) {
            curr_blocks++;
        }
    }

    /* Allocate new blocks if extending beyond current block count */
    uint32_t newly_allocated[PFS_DIRECT_BLOCKS];
    int newly_allocated_count = 0;

    if (req_blocks > curr_blocks) {
        for (uint32_t b = curr_blocks; b < req_blocks; b++) {
            uint32_t new_blk = 0;
            rc = pfs_alloc_block(&new_blk);
            if (rc != PFS_OK) {
                /* ROLLBACK all blocks allocated during this write attempt */
                for (int r = 0; r < newly_allocated_count; r++) {
                    pfs_free_block(newly_allocated[r]);
                }
                return rc;
            }
            newly_allocated[newly_allocated_count++] = new_blk;
            inode.direct[b] = new_blk;
        }
    }

    /* Perform block writes */
    uint32_t done = 0;
    while (done < length) {
        uint32_t cur_pos = offset + done;
        uint32_t blk_idx = cur_pos / PFS_SECTOR_SIZE;
        uint32_t blk_off = cur_pos % PFS_SECTOR_SIZE;
        uint32_t chunk = PFS_SECTOR_SIZE - blk_off;
        if (chunk > length - done) {
            chunk = length - done;
        }

        uint32_t phys_sec = s_pfs_sb.data_start + inode.direct[blk_idx];
        if (blk_off != 0 || chunk < PFS_SECTOR_SIZE) {
            rc = block_read(s_pfs_dev, phys_sec, s_pfs_data_buf);
            if (rc != BLOCK_OK) {
                return PFS_ERR_IO;
            }
        }

        kmemcpy(s_pfs_data_buf + blk_off, (const uint8_t *)buffer + done, chunk);
        rc = block_write(s_pfs_dev, phys_sec, s_pfs_data_buf);
        if (rc != BLOCK_OK) {
            return PFS_ERR_IO;
        }
        done += chunk;
    }

    /* Update inode size and commit to disk */
    if (end_pos > inode.size) {
        inode.size = end_pos;
    }
    rc = pfs_write_inode(&inode);
    if (rc != PFS_OK) {
        return rc;
    }

    *bytes_written = done;
    return PFS_OK;
}

/*
 * pfs_run_tests - In-kernel test suite executing format, mount, file creation,
 * write, multi-block read/write, directory creation, lookup, rollback, and corruption checks.
 */
int pfs_run_tests(void) {
    block_device_t *dev = block_get("ata0");
    if (!dev) {
        vga_puts("[PFS TEST] FAIL: ata0 block device not found\n");
        return PFS_ERR_NOT_FOUND;
    }

    /* Negative API check on unmounted volume */
    pfs_unmount();
    uint32_t dummy_ino, dummy_rw;
    if (pfs_create_file(PFS_ROOT_INODE, "test.txt", &dummy_ino) != PFS_ERR_NOT_MOUNTED ||
        pfs_read_file(1, 0, s_pfs_data_buf, 10, &dummy_rw) != PFS_ERR_NOT_MOUNTED ||
        pfs_write_file(1, 0, s_pfs_data_buf, 10, &dummy_rw) != PFS_ERR_NOT_MOUNTED) {
        vga_puts("[PFS TEST] FAIL: Unmounted operations not properly rejected\n");
        return PFS_ERR_INVALID;
    }

    /* 1. Format and Mount */
    int rc = pfs_format(dev);
    if (rc != PFS_OK) {
        vga_puts("[PFS TEST] FAIL: Format failed\n");
        return rc;
    }
    if (!pfs_is_mounted() || s_pfs_sb.free_inodes != s_pfs_sb.inode_count - 2) {
        vga_puts("[PFS TEST] FAIL: Mount status or initial free count invalid\n");
        return PFS_ERR_CORRUPT;
    }

    /* 2. File Creation & Data Verification */
    uint32_t f1_ino = 0;
    rc = pfs_create_file(PFS_ROOT_INODE, "hello.txt", &f1_ino);
    if (rc != PFS_OK || f1_ino <= PFS_ROOT_INODE) {
        vga_puts("[PFS TEST] FAIL: Create file hello.txt\n");
        return rc;
    }

    const char *msg = "Hello from MyOS persistent storage!";
    uint32_t msg_len = (uint32_t)kstrlen(msg);
    uint32_t written = 0;
    rc = pfs_write_file(f1_ino, 0, msg, msg_len, &written);
    if (rc != PFS_OK || written != msg_len) {
        vga_puts("[PFS TEST] FAIL: Write file hello.txt\n");
        return rc;
    }

    char read_buf[64];
    kmemset(read_buf, 0, sizeof(read_buf));
    uint32_t read_bytes = 0;
    rc = pfs_read_file(f1_ino, 0, read_buf, sizeof(read_buf) - 1, &read_bytes);
    if (rc != PFS_OK || read_bytes != msg_len || kstrcmp(read_buf, msg) != 0) {
        vga_puts("[PFS TEST] FAIL: Read file verify mismatch\n");
        return PFS_ERR_CORRUPT;
    }

    /* 3. Multi-block File Verification (1500 bytes spanning 3 blocks) */
    uint32_t f2_ino = 0;
    rc = pfs_create_file(PFS_ROOT_INODE, "multi.bin", &f2_ino);
    if (rc != PFS_OK) {
        vga_puts("[PFS TEST] FAIL: Create multi.bin\n");
        return rc;
    }

    static uint8_t multi_write_buf[1500];
    static uint8_t multi_read_buf[1500];
    for (int i = 0; i < 1500; i++) {
        multi_write_buf[i] = (uint8_t)(i ^ 0x5A);
        multi_read_buf[i] = 0;
    }
    rc = pfs_write_file(f2_ino, 0, multi_write_buf, 1500, &written);
    if (rc != PFS_OK || written != 1500) {
        vga_puts("[PFS TEST] FAIL: Write multi.bin\n");
        return rc;
    }

    rc = pfs_read_file(f2_ino, 0, multi_read_buf, 1500, &read_bytes);
    if (rc != PFS_OK || read_bytes != 1500) {
        vga_puts("[PFS TEST] FAIL: Read multi.bin\n");
        return rc;
    }
    for (int i = 0; i < 1500; i++) {
        if (multi_read_buf[i] != multi_write_buf[i]) {
            vga_puts("[PFS TEST] FAIL: Multi-block byte mismatch\n");
            return PFS_ERR_CORRUPT;
        }
    }

    /* 4. Directory Creation & Nested Lookup */
    uint32_t dir_ino = 0;
    rc = pfs_create_directory(PFS_ROOT_INODE, "subfolder", &dir_ino);
    if (rc != PFS_OK) {
        vga_puts("[PFS TEST] FAIL: Create directory subfolder\n");
        return rc;
    }

    uint32_t subfile_ino = 0;
    rc = pfs_create_file(dir_ino, "inner.txt", &subfile_ino);
    if (rc != PFS_OK) {
        vga_puts("[PFS TEST] FAIL: Create inner.txt in subfolder\n");
        return rc;
    }

    uint32_t looked_ino = 0;
    uint8_t looked_type = 0;
    rc = pfs_lookup(dir_ino, "inner.txt", &looked_ino, &looked_type);
    if (rc != PFS_OK || looked_ino != subfile_ino || looked_type != PFS_ENTRY_FILE) {
        vga_puts("[PFS TEST] FAIL: Lookup inner.txt failed\n");
        return PFS_ERR_NOT_FOUND;
    }

    /* 5. Negative Validation: Duplicate & Sparse Checks */
    if (pfs_create_file(PFS_ROOT_INODE, "hello.txt", &dummy_ino) != PFS_ERR_EXIST) {
        vga_puts("[PFS TEST] FAIL: Duplicate name was not rejected\n");
        return PFS_ERR_EXIST;
    }
    if (pfs_write_file(f1_ino, 5000, "abc", 3, &dummy_rw) != PFS_ERR_RANGE) {
        vga_puts("[PFS TEST] FAIL: Sparse write beyond file size was not rejected\n");
        return PFS_ERR_RANGE;
    }
    if (pfs_write_file(f1_ino, 0, multi_write_buf, PFS_MAX_FILE_SIZE + 1, &dummy_rw) != PFS_ERR_RANGE) {
        vga_puts("[PFS TEST] FAIL: Write exceeding max file size was not rejected\n");
        return PFS_ERR_RANGE;
    }

    /* 6. Block Zeroing Proof */
    uint32_t zf_ino = 0;
    pfs_create_file(PFS_ROOT_INODE, "zero_test.bin", &zf_ino);
    uint8_t secret_pat[512];
    kmemset(secret_pat, 0x55, 512);
    pfs_write_file(zf_ino, 0, secret_pat, 512, &written);
    /* Free the file and block */
    pfs_free_inode(zf_ino);
    /* Allocate a new block directly */
    uint32_t reallocated_blk = 0;
    rc = pfs_alloc_block(&reallocated_blk);
    if (rc == PFS_OK) {
        /* Read disk block directly and verify it was zeroed */
        uint8_t check_zero[512];
        block_read(dev, s_pfs_sb.data_start + reallocated_blk, check_zero);
        for (int i = 0; i < 512; i++) {
            if (check_zero[i] != 0) {
                vga_puts("[PFS TEST] FAIL: Block was not zeroed upon reallocation\n");
                return PFS_ERR_CORRUPT;
            }
        }
        pfs_free_block(reallocated_blk);
    }

    /* 7. Multi-Block Write Rollback Test */
    uint32_t rb_ino = 0;
    pfs_create_file(PFS_ROOT_INODE, "rollback.bin", &rb_ino);
    uint32_t orig_free = s_pfs_sb.free_blocks;
    /* Temporarily claim all but 2 free blocks */
    s_pfs_sb.free_blocks = 2;
    /* Attempt 3-block write (1500 bytes) -> should allocate 2, fail on 3rd, and rollback! */
    rc = pfs_write_file(rb_ino, 0, multi_write_buf, 1500, &written);
    if (rc != PFS_ERR_NOSPACE) {
        vga_puts("[PFS TEST] FAIL: Multi-block write did not fail on out-of-space\n");
        return PFS_ERR_INVALID;
    }
    pfs_inode_t rb_ino_check;
    pfs_read_inode(rb_ino, &rb_ino_check);
    if (rb_ino_check.size != 0 || rb_ino_check.direct[0] != PFS_NO_BLOCK || rb_ino_check.direct[1] != PFS_NO_BLOCK) {
        vga_puts("[PFS TEST] FAIL: Inode was not rolled back cleanly\n");
        return PFS_ERR_CORRUPT;
    }
    s_pfs_sb.free_blocks = orig_free;
    pfs_free_inode(rb_ino);

    /* 8. Superblock Corruption Rejection Test */
    pfs_unmount();
    block_read(dev, 0, s_pfs_sb_buf);
    pfs_superblock_t *corrupt_sb = (pfs_superblock_t *)s_pfs_sb_buf;
    corrupt_sb->magic = 0xDEADBEEF;
    block_write(dev, 0, s_pfs_sb_buf);

    if (pfs_mount(dev) != PFS_ERR_CORRUPT) {
        vga_puts("[PFS TEST] FAIL: Corrupted magic was not rejected by mount\n");
        return PFS_ERR_CORRUPT;
    }

    /* Restore clean format for normal operation */
    rc = pfs_format(dev);
    if (rc != PFS_OK) {
        vga_puts("[PFS TEST] FAIL: Re-format after corruption failed\n");
        return rc;
    }

    /* Create persistent hello.txt for reboot test */
    rc = pfs_create_file(PFS_ROOT_INODE, "hello.txt", &f1_ino);
    if (rc == PFS_OK) {
        pfs_write_file(f1_ino, 0, msg, msg_len, &written);
    }
    pfs_sync_superblock();

    return PFS_OK;
}
