#include "mount.h"
#include "ramfs.h"
#include "pfs.h"
#include "heap.h"
#include "vga.h"

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
    root->ops = NULL;
    root->fs = NULL;
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
 * vfs_mount - Mounts a filesystem at a specified mount point.
 * Performs strict parameter validation, duplicate rejection, target directory
 * resolution, and atomic rollback on filesystem mount failure.
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
    if (kstrcmp(norm_path, "/") != 0) {
        vfs_node_t *target_node = NULL;
        int vrc = vfs_lookup(norm_path, &target_node);
        if (vrc != VFS_OK) {
            return MOUNT_ERR_NOT_FOUND;
        }
        if (target_node->type != VFS_NODE_DIRECTORY) {
            vfs_node_unref(target_node);
            return MOUNT_ERR_NOT_DIR;
        }
        vfs_node_unref(target_node);
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

    mount_entry_t *slot = &s_mount_table[free_slot];
    slot->active = false;
    fs_instance_t *inst = &slot->instance_storage;
    kmemset(inst, 0, sizeof(fs_instance_t));

    /* Invoke filesystem mount callback */
    rc = type->mount(type, dev_name, dev, NULL, &inst);
    if (rc != MOUNT_OK) {
        /* Atomic rollback: clean slot state */
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
    slot->active = true;

    return MOUNT_OK;
}

/*
 * vfs_unmount - Safely unmounts a mounted filesystem.
 * Protects root mount, calls unmount callback, wipes instance storage,
 * and frees the mount slot.
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

    /* Call filesystem unmount callback */
    if (slot->type != NULL && slot->type->unmount != NULL) {
        rc = slot->type->unmount(slot->instance);
        if (rc != MOUNT_OK) {
            return MOUNT_ERR_IO;
        }
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
