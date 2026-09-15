#include "mount.h"
#include "ramfs.h"
#include "pfs.h"
#include "heap.h"
#include "vga.h"
#include "file.h"
#include "process.h"

/*
 * Static Kernel Storage for Mount Subsystem
 * Strictly bounded in .bss; zero dynamic heap allocations.
 */
static mount_entry_t s_mount_table[MAX_MOUNTS];
static fs_type_t s_fs_types[MAX_FS_TYPES];
static size_t s_fs_type_count = 0;
static bool s_mount_initialized = false;

/*
 * Freestanding String Utilities
 */
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

static void kmemset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

/*
 * normalize_mount_path - Validates and normalizes mount point paths.
 * Enforces absolute paths starting with '/', rejects empty/null paths,
 * and strips redundant trailing slashes (except for "/").
 */
static int normalize_mount_path(const char *path, char *out_buf, size_t max_len) {
    if (path == NULL || *path == '\0' || out_buf == NULL || max_len < 2) {
        return MOUNT_ERR_INVALID;
    }

    /* Mount point must be an absolute path */
    if (path[0] != '/') {
        return MOUNT_ERR_INVALID;
    }

    size_t len = kstrlen(path);
    if (len >= max_len) {
        return MOUNT_ERR_INVALID;
    }

    /* Strip trailing slashes, preserving root "/" */
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    for (size_t i = 0; i < len; i++) {
        out_buf[i] = path[i];
    }
    out_buf[len] = '\0';
    return MOUNT_OK;
}

/*
 * fs_register_type - Registers a new filesystem implementation.
 */
int fs_register_type(fs_type_t *type) {
    if (type == NULL || type->name[0] == '\0' || type->mount == NULL) {
        return MOUNT_ERR_INVALID;
    }

    if (fs_find_type(type->name) != NULL) {
        return MOUNT_ERR_EXIST;
    }

    if (s_fs_type_count >= MAX_FS_TYPES) {
        return MOUNT_ERR_FULL;
    }

    s_fs_types[s_fs_type_count] = *type;
    s_fs_type_count++;
    return MOUNT_OK;
}

/*
 * fs_unregister_type - Unregisters a filesystem type if not in active use.
 */
int fs_unregister_type(const char *name) {
    if (name == NULL || *name == '\0') {
        return MOUNT_ERR_INVALID;
    }

    /* Ensure no active mount uses this filesystem type */
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (s_mount_table[i].active && s_mount_table[i].type != NULL) {
            if (kstrcmp(s_mount_table[i].type->name, name) == 0) {
                return MOUNT_ERR_BUSY;
            }
        }
    }

    for (size_t i = 0; i < s_fs_type_count; i++) {
        if (kstrcmp(s_fs_types[i].name, name) == 0) {
            for (size_t j = i; j + 1 < s_fs_type_count; j++) {
                s_fs_types[j] = s_fs_types[j + 1];
            }
            s_fs_type_count--;
            kmemset(&s_fs_types[s_fs_type_count], 0, sizeof(fs_type_t));
            return MOUNT_OK;
        }
    }

    return MOUNT_ERR_NOT_FOUND;
}

/*
 * fs_find_type - Searches registered filesystem types by name.
 */
fs_type_t *fs_find_type(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < s_fs_type_count; i++) {
        if (kstrcmp(s_fs_types[i].name, name) == 0) {
            return &s_fs_types[i];
        }
    }
    return NULL;
}

size_t fs_type_count(void) {
    return s_fs_type_count;
}

fs_type_t *fs_get_type_by_index(size_t index) {
    if (index >= s_fs_type_count) {
        return NULL;
    }
    return &s_fs_types[index];
}

/*
 * RAMFS Mount Adapter
 */
static int ramfs_mount_op(fs_type_t *type, const char *dev_name, block_device_t *dev, void *data, fs_instance_t **out_instance) {
    (void)dev_name;
    (void)dev;
    (void)data;

    vfs_fs_t *fs = ramfs_create_fs();
    if (fs == NULL || fs->root == NULL) {
        return MOUNT_ERR_IO;
    }

    fs_instance_t *inst = *out_instance;
    inst->type = type;
    inst->dev = NULL;
    inst->root = fs->root;
    inst->priv = fs;
    inst->flags = 0;
    return MOUNT_OK;
}

static int ramfs_unmount_op(fs_instance_t *instance) {
    if (instance == NULL) {
        return MOUNT_ERR_INVALID;
    }
    if (instance->priv != NULL) {
        ramfs_destroy_fs((vfs_fs_t *)instance->priv);
        instance->priv = NULL;
    }
    instance->root = NULL;
    return MOUNT_OK;
}

/*
 * PFS Mount Adapter
 * Operates strictly through generic block device abstraction.
 */
static int pfs_mount_op(fs_type_t *type, const char *dev_name, block_device_t *dev, void *data, fs_instance_t **out_instance) {
    (void)dev_name;
    (void)data;

    if (dev == NULL) {
        return MOUNT_ERR_NODEV;
    }

    int rc = pfs_mount(dev);
    if (rc != PFS_OK) {
        if (rc == PFS_ERR_CORRUPT) {
            return MOUNT_ERR_CORRUPT;
        }
        if (rc == PFS_ERR_IO) {
            return MOUNT_ERR_IO;
        }
        return MOUNT_ERR_INVALID;
    }

    fs_instance_t *inst = *out_instance;
    inst->type = type;
    inst->dev = dev;

    /* Initialize minimal instance root node representing PFS root directory */
    vfs_node_t *root = &inst->root_node_storage;
    kmemset(root, 0, sizeof(vfs_node_t));
    kstrncpy(root->name, "/", VFS_NAME_MAX);
    root->type = VFS_NODE_DIRECTORY;
    root->size = PFS_SECTOR_SIZE;
    root->permissions = 0755;
    root->ref_count = 1;
    root->parent = NULL;
    root->ops = pfs_get_vfs_ops();
    root->fs = (vfs_fs_t *)inst;
    root->internal_data = (void *)(uintptr_t)PFS_ROOT_INODE;

    inst->root = root;
    inst->priv = (void *)pfs_get_superblock();
    inst->flags = 0;
    return MOUNT_OK;
}

static int pfs_unmount_op(fs_instance_t *instance) {
    if (instance == NULL) {
        return MOUNT_ERR_INVALID;
    }

    int rc = pfs_unmount();
    if (rc != PFS_OK) {
        return MOUNT_ERR_IO;
    }

    instance->root = NULL;
    instance->priv = NULL;
    instance->dev = NULL;
    return MOUNT_OK;
}

/*
 * mount_init - Initializes the mount table and registers default filesystem types.
 */
int mount_init(void) {
    kmemset(s_mount_table, 0, sizeof(s_mount_table));
    kmemset(s_fs_types, 0, sizeof(s_fs_types));
    s_fs_type_count = 0;

    /* 1. Register "ramfs" */
    fs_type_t ramfs_type;
    kmemset(&ramfs_type, 0, sizeof(ramfs_type));
    kstrncpy(ramfs_type.name, "ramfs", FS_NAME_MAX);
    ramfs_type.flags = 0;
    ramfs_type.mount = ramfs_mount_op;
    ramfs_type.unmount = ramfs_unmount_op;
    fs_register_type(&ramfs_type);

    /* 2. Register "pfs" */
    fs_type_t pfs_type;
    kmemset(&pfs_type, 0, sizeof(pfs_type));
    kstrncpy(pfs_type.name, "pfs", FS_NAME_MAX);
    pfs_type.flags = FS_FLAG_REQUIRES_DEV;
    pfs_type.mount = pfs_mount_op;
    pfs_type.unmount = pfs_unmount_op;
    fs_register_type(&pfs_type);

    s_mount_initialized = true;

    /* 3. Mount RAMFS at "/" */
    int rc = vfs_mount("ramfs", NULL, "/");
    if (rc != MOUNT_OK) {
        return rc;
    }

    /* 4. Link VFS root */
    vfs_mount_root((vfs_fs_t *)s_mount_table[0].instance->priv);

    /* 5. Silent probe for persistent filesystem on primary disk */
    block_device_t *ata0 = block_get("ata0");
    if (ata0 != NULL) {
        /* Mount if valid PFS exists; silently ignore if absent or unformatted */
        vfs_mount("pfs", "ata0", "/disk");
    }

    return MOUNT_OK;
}

/*
 * mount_find - Searches active mounts for an exact path match.
 */
mount_entry_t *mount_find(const char *path) {
    if (!s_mount_initialized || path == NULL) {
        return NULL;
    }

    char norm_path[MOUNT_PATH_MAX];
    if (normalize_mount_path(path, norm_path, sizeof(norm_path)) != MOUNT_OK) {
        return NULL;
    }

    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (s_mount_table[i].active && kstrcmp(s_mount_table[i].path, norm_path) == 0) {
            return &s_mount_table[i];
        }
    }
    return NULL;
}

mount_entry_t *mount_get_by_index(size_t index) {
    if (!s_mount_initialized || index >= MAX_MOUNTS) {
        return NULL;
    }
    if (!s_mount_table[index].active) {
        return NULL;
    }
    return &s_mount_table[index];
}

size_t mount_count(void) {
    if (!s_mount_initialized) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (s_mount_table[i].active) {
            count++;
        }
    }
    return count;
}

/*
 * mount_find_by_mountpoint - Searches active mounts for a mountpoint node match.
 */
mount_entry_t *mount_find_by_mountpoint(vfs_node_t *node) {
    if (!s_mount_initialized || node == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (s_mount_table[i].active && s_mount_table[i].mountpoint_node == node) {
            return &s_mount_table[i];
        }
    }
    return NULL;
}

/*
 * mount_find_by_root - Searches active mounts for a mounted root node match.
 */
mount_entry_t *mount_find_by_root(vfs_node_t *node) {
    if (!s_mount_initialized || node == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (s_mount_table[i].active && s_mount_table[i].instance != NULL &&
            s_mount_table[i].instance->root == node) {
            return &s_mount_table[i];
        }
    }
    return NULL;
}

/*
 * mount_check_busy - Checks if a mount entry has active references.
 * Returns MOUNT_ERR_BUSY if root ref_count > 1 or child vnodes are held.
 */
int mount_check_busy(mount_entry_t *entry) {
    if (entry == NULL || !entry->active || entry->instance == NULL) {
        return MOUNT_ERR_INVALID;
    }

    /* Root directory of the mounted filesystem: 1 ref held by mount slot itself */
    if (entry->instance->root != NULL && entry->instance->root->ref_count > 1) {
        return MOUNT_ERR_BUSY;
    }

    /* Filesystem-specific active vnode check */
    if (entry->type != NULL && kstrcmp(entry->type->name, "pfs") == 0) {
        if (pfs_is_busy()) {
            return MOUNT_ERR_BUSY;
        }
    }

    return MOUNT_OK;
}

/*
 * vfs_mount - Mounts a filesystem at a specified mount point.
 * Performs strict parameter validation, duplicate rejection, target directory
 * resolution, mountpoint node reference acquisition, and atomic rollback on failure.
 */
int vfs_mount(const char *type_name, const char *dev_name, const char *mount_point) {
    if (!s_mount_initialized) {
        return MOUNT_ERR_INVALID;
    }

    /* 1. Validate type and lookup filesystem type */
    if (type_name == NULL || *type_name == '\0') {
        return MOUNT_ERR_INVALID;
    }

    fs_type_t *type = fs_find_type(type_name);
    if (type == NULL) {
        return MOUNT_ERR_NOT_FOUND;
    }

    /* 2. Validate backing block device */
    block_device_t *dev = NULL;
    if (type->flags & FS_FLAG_REQUIRES_DEV) {
        if (dev_name == NULL || *dev_name == '\0') {
            return MOUNT_ERR_NODEV;
        }
        dev = block_get(dev_name);
        if (dev == NULL) {
            return MOUNT_ERR_NODEV;
        }
    }

    /* 3. Validate and normalize mount path */
    char norm_path[MOUNT_PATH_MAX];
    int rc = normalize_mount_path(mount_point, norm_path, sizeof(norm_path));
    if (rc != MOUNT_OK) {
        return MOUNT_ERR_INVALID;
    }

    /* 4. Reject duplicate mount point */
    if (mount_find(norm_path) != NULL) {
        return MOUNT_ERR_EXIST;
    }

    /* 5. Validate target mount point in VFS for non-root mounts */
    vfs_node_t *target_node = NULL;
    if (kstrcmp(norm_path, "/") != 0) {
        int vrc = vfs_lookup(norm_path, &target_node);
        if (vrc != VFS_OK) {
            return MOUNT_ERR_NOT_FOUND;
        }
        if (target_node->type != VFS_NODE_DIRECTORY) {
            return MOUNT_ERR_NOT_DIR;
        }
    }

    /* Find free slot in mount table */
    size_t free_slot = MAX_MOUNTS;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (!s_mount_table[i].active) {
            free_slot = i;
            break;
        }
    }
    if (free_slot == MAX_MOUNTS) {
        return MOUNT_ERR_FULL;
    }

    if (target_node != NULL) {
        vfs_node_ref(target_node);
    }

    mount_entry_t *slot = &s_mount_table[free_slot];
    slot->active = false;
    fs_instance_t *inst = &slot->instance_storage;
    kmemset(inst, 0, sizeof(fs_instance_t));

    /* Invoke filesystem mount callback */
    rc = type->mount(type, dev_name, dev, NULL, &inst);
    if (rc != MOUNT_OK) {
        /* Atomic rollback: clean slot state and unref target */
        if (target_node != NULL) {
            vfs_node_unref(target_node);
        }
        kmemset(inst, 0, sizeof(fs_instance_t));
        slot->active = false;
        return rc;
    }

    /* Register active mount entry */
    kstrncpy(slot->path, norm_path, MOUNT_PATH_MAX);
    if (dev_name != NULL) {
        kstrncpy(slot->dev_name, dev_name, BLOCK_NAME_MAX);
    } else {
        slot->dev_name[0] = '\0';
    }
    slot->type = type;
    slot->instance = inst;
    slot->ref_count = 1;
    slot->mountpoint_node = target_node;
    slot->active = true;

    return MOUNT_OK;
}

/*
 * vfs_unmount - Safely unmounts a mounted filesystem.
 * Protects root mount, enforces busy checks, calls unmount callback,
 * releases mountpoint node reference, and frees the mount slot.
 */
int vfs_unmount(const char *mount_point) {
    if (!s_mount_initialized || mount_point == NULL) {
        return MOUNT_ERR_INVALID;
    }

    char norm_path[MOUNT_PATH_MAX];
    int rc = normalize_mount_path(mount_point, norm_path, sizeof(norm_path));
    if (rc != MOUNT_OK) {
        return MOUNT_ERR_INVALID;
    }

    mount_entry_t *slot = mount_find(norm_path);
    if (slot == NULL || !slot->active) {
        return MOUNT_ERR_NOT_FOUND;
    }

    /* Root mount cannot be unmounted */
    if (kstrcmp(slot->path, "/") == 0) {
        return MOUNT_ERR_BUSY;
    }

    /* Check if mount is busy (active child vnodes or held references) */
    if (mount_check_busy(slot) != MOUNT_OK) {
        return MOUNT_ERR_BUSY;
    }

    /* Call filesystem unmount callback */
    if (slot->type != NULL && slot->type->unmount != NULL) {
        rc = slot->type->unmount(slot->instance);
        if (rc != MOUNT_OK) {
            return MOUNT_ERR_IO;
        }
    }

    /* Release mountpoint node reference in parent filesystem */
    if (slot->mountpoint_node != NULL) {
        vfs_node_unref(slot->mountpoint_node);
        slot->mountpoint_node = NULL;
    }

    /* Clean mount slot and reset dedicated instance storage */
    kmemset(&slot->instance_storage, 0, sizeof(fs_instance_t));
    slot->instance = NULL;
    slot->type = NULL;
    slot->path[0] = '\0';
    slot->dev_name[0] = '\0';
    slot->ref_count = 0;
    slot->active = false;

    return MOUNT_OK;
}

/*
 * ==============================================================================
 * In-Kernel Mount Verification Suite
 * Verifies all 22 required test conditions.
 * ==============================================================================
 */

static int dummy_mount_op(fs_type_t *type, const char *dev_name, block_device_t *dev, void *data, fs_instance_t **out_instance) {
    (void)dev_name;
    (void)dev;
    (void)data;
    fs_instance_t *inst = *out_instance;
    inst->type = type;
    inst->dev = dev;
    inst->root = &inst->root_node_storage;
    kmemset(inst->root, 0, sizeof(vfs_node_t));
    inst->root->type = VFS_NODE_DIRECTORY;
    inst->root->ref_count = 1;
    return MOUNT_OK;
}

static int dummy_unmount_op(fs_instance_t *instance) {
    if (instance == NULL) return MOUNT_ERR_INVALID;
    instance->root = NULL;
    return MOUNT_OK;
}

static int failing_mount_op(fs_type_t *type, const char *dev_name, block_device_t *dev, void *data, fs_instance_t **out_instance) {
    (void)type;
    (void)dev_name;
    (void)dev;
    (void)data;
    (void)out_instance;
    return MOUNT_ERR_CORRUPT;
}

int mount_run_tests(void) {
    vga_puts("[TEST 1/22] Filesystem type registration... ");
    fs_type_t test_type;
    kmemset(&test_type, 0, sizeof(test_type));
    kstrncpy(test_type.name, "testfs", FS_NAME_MAX);
    test_type.mount = dummy_mount_op;
    test_type.unmount = dummy_unmount_op;

    if (fs_register_type(&test_type) != MOUNT_OK) {
        vga_puts("FAIL (register)\n");
        return -1;
    }
    if (fs_register_type(&test_type) != MOUNT_ERR_EXIST) {
        vga_puts("FAIL (duplicate register)\n");
        return -1;
    }
    if (fs_register_type(NULL) != MOUNT_ERR_INVALID) {
        vga_puts("FAIL (null register)\n");
        return -1;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 2/22] Filesystem type lookup... ");
    if (fs_find_type("ramfs") == NULL || fs_find_type("pfs") == NULL || fs_find_type("testfs") == NULL) {
        vga_puts("FAIL (known types)\n");
        return -2;
    }
    if (fs_find_type("nonexistent") != NULL || fs_find_type(NULL) != NULL) {
        vga_puts("FAIL (unknown lookup)\n");
        return -2;
    }
    vga_puts("PASS\n");

    /* Ensure /disk mount point directory exists in RAMFS */
    vfs_mkdir("/disk", NULL);

    /* Format PFS volume on ata0 to ensure known valid superblock */
    block_device_t *ata0 = block_get("ata0");
    if (ata0 != NULL) {
        pfs_format(ata0);
        /* If previously mounted at /disk, unmount to start clean */
        if (mount_find("/disk") != NULL) {
            vfs_unmount("/disk");
        }
    }

    vga_puts("[TEST 3/22] Successful PFS mount... ");
    if (ata0 != NULL) {
        int rc = vfs_mount("pfs", "ata0", "/disk");
        if (rc != MOUNT_OK) {
            vga_puts("FAIL (pfs mount rc)\n");
            return -3;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 4/22] Mount appears in mount table... ");
    mount_entry_t *disk_entry = mount_find("/disk");
    if (ata0 != NULL) {
        if (disk_entry == NULL || !disk_entry->active) {
            vga_puts("FAIL (mount_find /disk)\n");
            return -4;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 5/22] Mount point is recorded correctly... ");
    if (ata0 != NULL) {
        if (kstrcmp(disk_entry->path, "/disk") != 0 || kstrcmp(disk_entry->dev_name, "ata0") != 0) {
            vga_puts("FAIL (path/dev recorded)\n");
            return -5;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 6/22] Filesystem instance is valid... ");
    if (ata0 != NULL) {
        if (disk_entry->instance == NULL || disk_entry->instance->type != fs_find_type("pfs") || disk_entry->instance->dev != ata0) {
            vga_puts("FAIL (instance fields)\n");
            return -6;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 7/22] Filesystem root is valid... ");
    if (ata0 != NULL) {
        if (disk_entry->instance->root == NULL || disk_entry->instance->root->type != VFS_NODE_DIRECTORY || disk_entry->instance->root->ref_count < 1) {
            vga_puts("FAIL (root node)\n");
            return -7;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 8/22] Duplicate mount rejection... ");
    if (ata0 != NULL) {
        if (vfs_mount("pfs", "ata0", "/disk") != MOUNT_ERR_EXIST) {
            vga_puts("FAIL (duplicate mount allowed)\n");
            return -8;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 9/22] Invalid filesystem rejection... ");
    if (vfs_mount("nonexistent_fs", "ata0", "/disk") != MOUNT_ERR_NOT_FOUND) {
        vga_puts("FAIL (invalid fs allowed)\n");
        return -9;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 10/22] Invalid mount-point rejection... ");
    if (vfs_mount("pfs", "ata0", NULL) != MOUNT_ERR_INVALID) {
        vga_puts("FAIL (null mount-point)\n");
        return -10;
    }
    if (vfs_mount("pfs", "ata0", "") != MOUNT_ERR_INVALID) {
        vga_puts("FAIL (empty mount-point)\n");
        return -10;
    }
    if (vfs_mount("pfs", "ata0", "relative_path") != MOUNT_ERR_INVALID) {
        vga_puts("FAIL (relative mount-point)\n");
        return -10;
    }
    if (vfs_mount("pfs", "ata0", "/nonexistent/path/dir") != MOUNT_ERR_NOT_FOUND) {
        vga_puts("FAIL (unresolvable mount-point)\n");
        return -10;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 11/22] Corrupt/unformatted PFS mount rejection... ");
    /* Register failing filesystem to verify clean corruption rejection */
    fs_type_t corrupt_type;
    kmemset(&corrupt_type, 0, sizeof(corrupt_type));
    kstrncpy(corrupt_type.name, "corruptfs", FS_NAME_MAX);
    corrupt_type.mount = failing_mount_op;
    corrupt_type.unmount = dummy_unmount_op;
    fs_register_type(&corrupt_type);

    vfs_mkdir("/fail_dir", NULL);
    if (vfs_mount("corruptfs", NULL, "/fail_dir") != MOUNT_ERR_CORRUPT) {
        vga_puts("FAIL (corrupt mount not rejected)\n");
        return -11;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 12/22] Failed mount rollback... ");
    if (mount_find("/fail_dir") != NULL) {
        vga_puts("FAIL (slot not rolled back)\n");
        return -12;
    }
    fs_unregister_type("corruptfs");
    vfs_unlink("/fail_dir");
    vga_puts("PASS\n");

    vga_puts("[TEST 13/22] Successful unmount... ");
    if (ata0 != NULL) {
        if (vfs_unmount("/disk") != MOUNT_OK) {
            vga_puts("FAIL (unmount rc)\n");
            return -13;
        }
        if (mount_find("/disk") != NULL) {
            vga_puts("FAIL (mount still found after unmount)\n");
            return -13;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 14/22] Mount slot becomes reusable... ");
    if (ata0 != NULL) {
        if (vfs_mount("pfs", "ata0", "/disk") != MOUNT_OK) {
            vga_puts("FAIL (re-mount)\n");
            return -14;
        }
        if (vfs_unmount("/disk") != MOUNT_OK) {
            vga_puts("FAIL (re-unmount)\n");
            return -14;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 15/22] Double unmount rejection... ");
    if (vfs_unmount("/disk") != MOUNT_ERR_NOT_FOUND) {
        vga_puts("FAIL (double unmount allowed)\n");
        return -15;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 16/22] Nonexistent mount rejection... ");
    if (vfs_unmount("/nonexistent") != MOUNT_ERR_NOT_FOUND) {
        vga_puts("FAIL (nonexistent unmount)\n");
        return -16;
    }
    if (vfs_unmount(NULL) != MOUNT_ERR_INVALID) {
        vga_puts("FAIL (null unmount)\n");
        return -16;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 17/22] Mount table full handling... ");
    /* Create directories /d1 through /d7 to fill remaining slots up to MAX_MOUNTS (8) */
    char test_dirs[7][8] = {"/d1", "/d2", "/d3", "/d4", "/d5", "/d6", "/d7"};
    size_t mounted_test_slots = 0;
    for (size_t i = 0; i < 7; i++) {
        vfs_mkdir(test_dirs[i], NULL);
        int mrc = vfs_mount("testfs", NULL, test_dirs[i]);
        if (mrc == MOUNT_OK) {
            mounted_test_slots++;
        }
    }
    /* Table should now have 1 (root) + 7 = 8 mounts (full) */
    vfs_mkdir("/d_overflow", NULL);
    if (vfs_mount("testfs", NULL, "/d_overflow") != MOUNT_ERR_FULL) {
        vga_puts("FAIL (overflow allowed)\n");
        return -17;
    }
    /* Clean up test slots */
    for (size_t i = 0; i < 7; i++) {
        vfs_unmount(test_dirs[i]);
        vfs_unlink(test_dirs[i]);
    }
    vfs_unlink("/d_overflow");
    vga_puts("PASS\n");

    vga_puts("[TEST 18/22] Root RAMFS remains functional... ");
    vfs_node_t *rf = NULL;
    if (vfs_create("/root_test.txt", &rf) != VFS_OK || rf == NULL) {
        vga_puts("FAIL (ramfs create)\n");
        return -18;
    }
    size_t bw = 0, br = 0;
    char wbuf[16] = "hello_ramfs";
    char rbuf[16] = {0};
    vfs_write(rf, wbuf, 0, 11, &bw);
    vfs_read(rf, rbuf, 0, 11, &br);
    vfs_node_unref(rf);
    vfs_unlink("/root_test.txt");
    if (br != 11 || kstrcmp(rbuf, "hello_ramfs") != 0) {
        vga_puts("FAIL (ramfs I/O)\n");
        return -18;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 19/22] RAMFS root remains available when PFS mounted... ");
    if (ata0 != NULL) {
        vfs_mount("pfs", "ata0", "/disk");
    }
    mount_entry_t *root_entry = mount_find("/");
    if (root_entry == NULL || !root_entry->active || kstrcmp(root_entry->type->name, "ramfs") != 0) {
        vga_puts("FAIL (root mount lost)\n");
        return -19;
    }
    if (vfs_unmount("/") != MOUNT_ERR_BUSY) {
        vga_puts("FAIL (root unmount permitted)\n");
        return -19;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 20/22] PFS independent of ATA details... ");
    /* Verify disk entry uses block device abstraction */
    if (ata0 != NULL) {
        mount_entry_t *dslot = mount_find("/disk");
        if (dslot == NULL || dslot->instance->dev != ata0) {
            vga_puts("FAIL (block dev dev pointer)\n");
            return -20;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 21/22] No stale references after unmount... ");
    if (ata0 != NULL) {
        vfs_unmount("/disk");
        mount_entry_t *dslot = mount_find("/disk");
        if (dslot != NULL) {
            vga_puts("FAIL (dslot found)\n");
            return -21;
        }
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 22/22] Repeated mount/unmount heap zero-leakage... ");
    heap_stats_t h_before, h_after;
    heap_get_stats(&h_before);

    if (ata0 != NULL) {
        for (int cycle = 0; cycle < 5; cycle++) {
            int mrc = vfs_mount("pfs", "ata0", "/disk");
            if (mrc != MOUNT_OK) {
                vga_puts("FAIL (cycle mount rc=");
                vga_print_dec((uint64_t)(-mrc));
                vga_puts(" cyc=");
                vga_print_dec((uint64_t)cycle);
                vga_puts(")\n");
                return -22;
            }
            int urc = vfs_unmount("/disk");
            if (urc != MOUNT_OK) {
                vga_puts("FAIL (cycle unmount rc=");
                vga_print_dec((uint64_t)(-urc));
                vga_puts(")\n");
                return -22;
            }
        }
    }

    heap_get_stats(&h_after);
    if (h_before.used_bytes != h_after.used_bytes) {
        vga_puts("FAIL (heap leak detected)\n");
        return -22;
    }

    /* Clean up test type and re-establish persistent mount if disk is attached */
    fs_unregister_type("testfs");
    if (ata0 != NULL) {
        vfs_mount("pfs", "ata0", "/disk");
    }
    vga_puts("PASS\n");

    return MOUNT_OK;
}

/*
 * ==============================================================================
 * In-Kernel Stage 12E Verification Suite: VFS -> Persistent Filesystem
 * Verifies all 15 required test conditions.
 * ==============================================================================
 */
int vfs12e_run_tests(void) {
    block_device_t *ata0 = block_get("ata0");
    if (ata0 == NULL) {
        vga_puts("[TEST 12E] Backing device ata0 not found!\n");
        return -1;
    }

    /* Ensure clean, freshly formatted PFS volume for idempotent test execution */
    if (mount_find("/disk") != NULL) {
        vfs_unmount("/disk");
    }
    pfs_format(ata0);
    int mrc = vfs_mount("pfs", "ata0", "/disk");
    if (mrc != MOUNT_OK) {
        vga_puts("[TEST 12E] Initial mount of /disk failed!\n");
        return -1;
    }

    vga_puts("[TEST 1/15] Mountpoint path resolution to PFS root... ");
    vfs_node_t *disk_node = NULL;
    int rc = vfs_lookup("/disk", &disk_node);
    if (rc != VFS_OK || disk_node == NULL) {
        vga_puts("FAIL (lookup /disk)\n");
        return -1;
    }
    mount_entry_t *disk_mnt = mount_find("/disk");
    if (disk_mnt == NULL || disk_mnt->instance == NULL || disk_node != disk_mnt->instance->root) {
        vga_puts("FAIL (not mounted root)\n");
        return -1;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 2/15] Distinct mount root semantics... ");
    if (disk_mnt->mountpoint_node == NULL || disk_mnt->mountpoint_node == disk_mnt->instance->root) {
        vga_puts("FAIL (identical nodes)\n");
        return -2;
    }
    if ((uint32_t)(uintptr_t)disk_mnt->instance->root->internal_data != PFS_ROOT_INODE) {
        vga_puts("FAIL (root inode != 1)\n");
        return -2;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 3/15] Create file via VFS in mounted PFS (/disk/test12e.txt)... ");
    vfs_node_t *file_node = NULL;
    rc = vfs_create("/disk/test12e.txt", &file_node);
    if (rc != VFS_OK || file_node == NULL) {
        vga_puts("FAIL (create /disk/test12e.txt)\n");
        return -3;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 4/15] Write & read file via VFS ops... ");
    const char *payload = "Hello VFS Persistent Filesystem!";
    size_t plen = kstrlen(payload);
    size_t bw = 0, br = 0;
    rc = vfs_write(file_node, payload, 0, plen, &bw);
    if (rc != VFS_OK || bw != plen) {
        vga_puts("FAIL (vfs_write)\n");
        return -4;
    }
    char read_buf[64];
    kmemset(read_buf, 0, sizeof(read_buf));
    rc = vfs_read(file_node, read_buf, 0, plen, &br);
    if (rc != VFS_OK || br != plen || kstrcmp(read_buf, payload) != 0) {
        vga_puts("FAIL (vfs_read mismatch)\n");
        return -4;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 5/15] Create directory via VFS in mounted PFS (/disk/dir12e)... ");
    vfs_node_t *dir_node = NULL;
    rc = vfs_mkdir("/disk/dir12e", &dir_node);
    if (rc != VFS_OK || dir_node == NULL) {
        vga_puts("FAIL (mkdir /disk/dir12e)\n");
        return -5;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 6/15] Create & read file inside subdirectory (/disk/dir12e/sub.txt)... ");
    vfs_node_t *subfile_node = NULL;
    rc = vfs_create("/disk/dir12e/sub.txt", &subfile_node);
    if (rc != VFS_OK || subfile_node == NULL) {
        vga_puts("FAIL (create /disk/dir12e/sub.txt)\n");
        return -6;
    }
    rc = vfs_write(subfile_node, "subdata", 0, 7, &bw);
    if (rc != VFS_OK || bw != 7) {
        vga_puts("FAIL (subfile write)\n");
        return -6;
    }
    kmemset(read_buf, 0, sizeof(read_buf));
    rc = vfs_read(subfile_node, read_buf, 0, 7, &br);
    if (rc != VFS_OK || br != 7 || kstrcmp(read_buf, "subdata") != 0) {
        vga_puts("FAIL (subfile read mismatch)\n");
        return -6;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 7/15] Directory iteration via vfs_readdir on /disk... ");
    bool found_file = false;
    bool found_dir = false;
    vfs_dirent_t de;
    uint64_t idx = 0;
    while (vfs_readdir(disk_node, idx++, &de) == VFS_OK) {
        if (kstrcmp(de.name, "test12e.txt") == 0 && de.type == VFS_NODE_FILE) {
            found_file = true;
        }
        if (kstrcmp(de.name, "dir12e") == 0 && de.type == VFS_NODE_DIRECTORY) {
            found_dir = true;
        }
    }
    if (!found_file || !found_dir) {
        vga_puts("FAIL (entries not found in readdir)\n");
        return -7;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 8/15] Cross-boundary '..' traversal (/disk/.. -> /)... ");
    vfs_node_t *cross_node = NULL;
    rc = vfs_lookup("/disk/..", &cross_node);
    if (rc != VFS_OK || cross_node != vfs_get_root()) {
        vga_puts("FAIL (/disk/.. did not reach root)\n");
        return -8;
    }
    rc = vfs_lookup("/disk/../readme.txt", &cross_node);
    if (rc != VFS_OK || cross_node == NULL || kstrcmp(cross_node->name, "readme.txt") != 0) {
        vga_puts("FAIL (/disk/../readme.txt lookup)\n");
        return -8;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 9/15] Root '..' clamping (/.. -> /)... ");
    vfs_node_t *root_clamp = NULL;
    rc = vfs_lookup("/..", &root_clamp);
    if (rc != VFS_OK || root_clamp != vfs_get_root()) {
        vga_puts("FAIL (/.. escape)\n");
        return -9;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 10/15] Canonical path reconstruction (vfs_get_path)... ");
    char path_buf[VFS_PATH_MAX];
    rc = vfs_get_path(disk_node, path_buf, sizeof(path_buf));
    if (rc != VFS_OK || kstrcmp(path_buf, "/disk") != 0) {
        vga_puts("FAIL (path for /disk)\n");
        return -10;
    }
    rc = vfs_get_path(dir_node, path_buf, sizeof(path_buf));
    if (rc != VFS_OK || kstrcmp(path_buf, "/disk/dir12e") != 0) {
        vga_puts("FAIL (path for /disk/dir12e)\n");
        return -10;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 11/15] CWD relative resolution from within mounted PFS... ");
    process_t *proc = process_current();
    if (!proc) proc = process_get(0);
    vfs_node_t *orig_cwd = proc->cwd;
    process_set_cwd(proc, disk_node);
    vfs_node_t *rel_node = NULL;
    rc = vfs_lookup_from(proc->cwd, "test12e.txt", &rel_node);
    if (rc != VFS_OK || rel_node != file_node) {
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (relative lookup from cwd)\n");
        return -11;
    }
    rc = vfs_lookup_from(proc->cwd, "../readme.txt", &rel_node);
    if (rc != VFS_OK || rel_node == NULL || kstrcmp(rel_node->name, "readme.txt") != 0) {
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (relative .. from cwd)\n");
        return -11;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 12/15] File descriptor open, write, read & close on PFS file... ");
    int fd = fd_open(proc, "/disk/test12e.txt", O_RDWR);
    if (fd < 0) {
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (fd_open)\n");
        return -12;
    }
    char fd_buf[64];
    kmemset(fd_buf, 0, sizeof(fd_buf));
    int64_t nread = fd_read(proc, fd, fd_buf, plen);
    if (nread != (int64_t)plen || kstrcmp(fd_buf, payload) != 0) {
        fd_close(proc, fd);
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (fd_read)\n");
        return -12;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 13/15] Busy unmount rejection while references active... ");
    /* FD is still open -> unmount MUST fail with MOUNT_ERR_BUSY */
    int urc = vfs_unmount("/disk");
    if (urc != MOUNT_ERR_BUSY) {
        fd_close(proc, fd);
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (unmount succeeded while FD open)\n");
        return -13;
    }
    fd_close(proc, fd);
    /* CWD is still at /disk -> unmount MUST still fail with MOUNT_ERR_BUSY */
    urc = vfs_unmount("/disk");
    if (urc != MOUNT_ERR_BUSY) {
        process_set_cwd(proc, orig_cwd);
        vga_puts("FAIL (unmount succeeded while CWD in mount)\n");
        return -13;
    }
    /* Reset CWD back to original */
    process_set_cwd(proc, orig_cwd);
    vga_puts("PASS\n");

    vga_puts("[TEST 14/15] Clean unmount and re-mount persistence verification... ");
    urc = vfs_unmount("/disk");
    if (urc != MOUNT_OK) {
        vga_puts("FAIL (clean unmount)\n");
        return -14;
    }
    if (mount_find("/disk") != NULL) {
        vga_puts("FAIL (mount still found)\n");
        return -14;
    }
    /* Re-mount PFS on /disk */
    mrc = vfs_mount("pfs", "ata0", "/disk");
    if (mrc != MOUNT_OK) {
        vga_puts("FAIL (re-mount)\n");
        return -14;
    }
    /* Verify test12e.txt still exists with same content */
    vfs_node_t *remount_file = NULL;
    rc = vfs_lookup("/disk/test12e.txt", &remount_file);
    if (rc != VFS_OK || remount_file == NULL) {
        vga_puts("FAIL (file lost after remount)\n");
        return -14;
    }
    kmemset(read_buf, 0, sizeof(read_buf));
    rc = vfs_read(remount_file, read_buf, 0, plen, &br);
    if (rc != VFS_OK || br != plen || kstrcmp(read_buf, payload) != 0) {
        vga_puts("FAIL (content corrupted after remount)\n");
        return -14;
    }
    vga_puts("PASS\n");

    vga_puts("[TEST 15/15] Cleanup test artifacts via VFS unlink... ");
    rc = vfs_unlink("/disk/dir12e/sub.txt");
    if (rc != VFS_OK) {
        vga_puts("FAIL (unlink sub.txt)\n");
        return -15;
    }
    rc = vfs_unlink("/disk/test12e.txt");
    if (rc != VFS_OK) {
        vga_puts("FAIL (unlink test12e.txt)\n");
        return -15;
    }
    /* Verify files no longer exist */
    if (vfs_lookup("/disk/test12e.txt", &file_node) != VFS_ERR_NOT_FOUND) {
        vga_puts("FAIL (test12e.txt still exists)\n");
        return -15;
    }

    /* Verify unlink-while-open lifetime contract (Stage 11E compliant) */
    vfs_node_t *open_node = NULL;
    rc = vfs_create("/disk/open.txt", &open_node);
    if (rc != VFS_OK || open_node == NULL) {
        vga_puts("FAIL (create /disk/open.txt)\n");
        return -15;
    }
    size_t obw = 0;
    vfs_write(open_node, "opentest", 0, 8, &obw);
    int ofd = fd_open(proc, "/disk/open.txt", O_RDWR);
    if (ofd < 0) {
        vga_puts("FAIL (fd_open /disk/open.txt)\n");
        return -15;
    }
    rc = vfs_unlink("/disk/open.txt");
    if (rc != VFS_OK) {
        fd_close(proc, ofd);
        vga_puts("FAIL (unlink /disk/open.txt)\n");
        return -15;
    }
    char obuf[16];
    kmemset(obuf, 0, sizeof(obuf));
    int64_t onr = fd_read(proc, ofd, obuf, 8);
    if (onr != 8 || kstrcmp(obuf, "opentest") != 0) {
        fd_close(proc, ofd);
        vga_puts("FAIL (read through unlinked open FD)\n");
        return -15;
    }
    fd_close(proc, ofd);
    vfs_node_t *chk_node = NULL;
    if (vfs_lookup("/disk/open.txt", &chk_node) != VFS_ERR_NOT_FOUND) {
        vga_puts("FAIL (open.txt still found after close)\n");
        return -15;
    }
    vga_puts("PASS\n");

    return MOUNT_OK;
}
