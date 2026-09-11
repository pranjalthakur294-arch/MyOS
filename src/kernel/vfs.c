#include "vfs.h"
#include "vga.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Static VFS Mount State
 */
static vfs_fs_t *vfs_root_fs = NULL;
static vfs_node_t *vfs_root_node = NULL;

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

static int kstrncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

/*
 * vfs_init - Initializes the Virtual File System layer.
 * Keeps boot quiet (0 screen rows) to preserve screen budget.
 */
void vfs_init(void) {
    vfs_root_fs = NULL;
    vfs_root_node = NULL;
}

/*
 * vfs_mount_root - Mounts a filesystem at the VFS root ("/").
 */
int vfs_mount_root(vfs_fs_t *fs) {
    if (fs == NULL || fs->root == NULL) {
        return VFS_ERR_INVALID;
    }
    if (fs->root->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }

    vfs_root_fs = fs;
    vfs_root_node = fs->root;
    return VFS_OK;
}

/*
 * vfs_get_root - Returns the root directory node of the VFS.
 */
vfs_node_t *vfs_get_root(void) {
    return vfs_root_node;
}

/*
 * vfs_lookup - Resolves an absolute path to a VFS node.
 * Handles repeated slashes, verifies directory traversal, and enforces bounds.
 */
int vfs_lookup(const char *path, vfs_node_t **out_node) {
    if (path == NULL || out_node == NULL) {
        return VFS_ERR_INVALID;
    }
    if (vfs_root_node == NULL) {
        return VFS_ERR_IO;
    }

    /* Path must be absolute */
    if (path[0] != '/') {
        return VFS_ERR_INVALID;
    }

    size_t path_len = kstrlen(path);
    if (path_len >= VFS_PATH_MAX) {
        return VFS_ERR_PATH_TOO_LONG;
    }

    vfs_node_t *curr = vfs_root_node;
    const char *p = path;

    /* Skip leading slashes */
    while (*p == '/') {
        p++;
    }

    /* Root path ("/" or "///") */
    if (*p == '\0') {
        *out_node = vfs_root_node;
        return VFS_OK;
    }

    /* Iterate through path components */
    while (*p != '\0') {
        const char *comp_start = p;
        size_t comp_len = 0;
        while (*p != '\0' && *p != '/') {
            comp_len++;
            p++;
        }

        if (comp_len >= VFS_NAME_MAX) {
            return VFS_ERR_NAME_TOO_LONG;
        }

        char comp_name[VFS_NAME_MAX];
        for (size_t i = 0; i < comp_len; i++) {
            comp_name[i] = comp_start[i];
        }
        comp_name[comp_len] = '\0';

        /* Cannot traverse through non-directory nodes */
        if (curr->type != VFS_NODE_DIRECTORY) {
            return VFS_ERR_NOT_DIR;
        }
        if (curr->ops == NULL || curr->ops->lookup == NULL) {
            return VFS_ERR_NOT_SUPPORTED;
        }

        vfs_node_t *next_node = NULL;
        int err = curr->ops->lookup(curr, comp_name, &next_node);
        if (err != VFS_OK) {
            return err;
        }
        curr = next_node;

        /* Skip trailing/redundant slashes */
        while (*p == '/') {
            p++;
        }
    }

    *out_node = curr;
    return VFS_OK;
}

/*
 * split_parent_and_leaf - Helper to decompose an absolute path into:
 *   - parent directory path (stored in parent_buf)
 *   - leaf name (stored in leaf_buf)
 */
static int split_parent_and_leaf(const char *path, char *parent_buf, char *leaf_buf) {
    if (path == NULL || parent_buf == NULL || leaf_buf == NULL) {
        return VFS_ERR_INVALID;
    }
    if (path[0] != '/') {
        return VFS_ERR_INVALID;
    }

    size_t len = kstrlen(path);
    if (len >= VFS_PATH_MAX) {
        return VFS_ERR_PATH_TOO_LONG;
    }

    /* Strip trailing slashes to find leaf name */
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    /* Root cannot be created/mkdir'd */
    if (len <= 1 && path[0] == '/') {
        return VFS_ERR_EXISTS;
    }

    /* Find last slash */
    size_t last_slash = len - 1;
    while (last_slash > 0 && path[last_slash] != '/') {
        last_slash--;
    }

    /* Leaf component starts at last_slash + 1 */
    size_t leaf_len = len - (last_slash + 1);
    if (leaf_len == 0 || leaf_len >= VFS_NAME_MAX) {
        return (leaf_len >= VFS_NAME_MAX) ? VFS_ERR_NAME_TOO_LONG : VFS_ERR_INVALID;
    }

    for (size_t i = 0; i < leaf_len; i++) {
        leaf_buf[i] = path[last_slash + 1 + i];
    }
    leaf_buf[leaf_len] = '\0';

    /* Parent directory path */
    if (last_slash == 0) {
        parent_buf[0] = '/';
        parent_buf[1] = '\0';
    } else {
        for (size_t i = 0; i < last_slash; i++) {
            parent_buf[i] = path[i];
        }
        parent_buf[last_slash] = '\0';
    }

    return VFS_OK;
}

/*
 * vfs_create - Creates a regular file at the specified absolute path.
 */
int vfs_create(const char *path, vfs_node_t **out_node) {
    char parent_path[VFS_PATH_MAX];
    char leaf_name[VFS_NAME_MAX];

    int err = split_parent_and_leaf(path, parent_path, leaf_name);
    if (err != VFS_OK) {
        return err;
    }

    vfs_node_t *parent_node = NULL;
    err = vfs_lookup(parent_path, &parent_node);
    if (err != VFS_OK) {
        return err;
    }

    if (parent_node->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    if (parent_node->ops == NULL || parent_node->ops->create == NULL) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    return parent_node->ops->create(parent_node, leaf_name, out_node);
}

/*
 * vfs_mkdir - Creates a directory at the specified absolute path.
 */
int vfs_mkdir(const char *path, vfs_node_t **out_node) {
    char parent_path[VFS_PATH_MAX];
    char leaf_name[VFS_NAME_MAX];

    int err = split_parent_and_leaf(path, parent_path, leaf_name);
    if (err != VFS_OK) {
        return err;
    }

    vfs_node_t *parent_node = NULL;
    err = vfs_lookup(parent_path, &parent_node);
    if (err != VFS_OK) {
        return err;
    }

    if (parent_node->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    if (parent_node->ops == NULL || parent_node->ops->mkdir == NULL) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    return parent_node->ops->mkdir(parent_node, leaf_name, out_node);
}

/*
 * vfs_readdir - Reads a directory entry at the specified index from a directory node.
 * Returns VFS_OK on success, VFS_EOF at end of directory, or negative error code.
 */
int vfs_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent) {
    if (dir == NULL || dirent == NULL) {
        return VFS_ERR_INVALID;
    }
    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    if (dir->ops == NULL || dir->ops->readdir == NULL) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    return dir->ops->readdir(dir, index, dirent);
}

/*
 * vfs_read - Reads data from a VFS file node.
 */
int vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read) {
    if (size == 0) {
        if (bytes_read != NULL) {
            *bytes_read = 0;
        }
        return VFS_OK;
    }
    if (node == NULL || buffer == NULL || bytes_read == NULL) {
        return VFS_ERR_INVALID;
    }
    if (node->type == VFS_NODE_DIRECTORY) {
        return VFS_ERR_IS_DIR;
    }
    if (node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID;
    }
    if (node->ops == NULL || node->ops->read == NULL) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    return node->ops->read(node, buffer, offset, size, bytes_read);
}

/*
 * vfs_write - Writes data into a VFS file node.
 */
int vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written) {
    if (size == 0) {
        if (bytes_written != NULL) {
            *bytes_written = 0;
        }
        return VFS_OK;
    }
    if (node == NULL || buffer == NULL || bytes_written == NULL) {
        return VFS_ERR_INVALID;
    }
    if (node->type == VFS_NODE_DIRECTORY) {
        return VFS_ERR_IS_DIR;
    }
    if (node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID;
    }
    if (node->ops == NULL || node->ops->write == NULL) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    return node->ops->write(node, buffer, offset, size, bytes_written);
}

/*
 * vfs_strerror - Returns descriptive text for VFS error codes.
 */
const char *vfs_strerror(int err) {
    switch (err) {
        case VFS_OK:
            return "Success";
        case VFS_ERR_INVALID:
            return "Invalid argument";
        case VFS_ERR_NOT_FOUND:
            return "File or path not found";
        case VFS_ERR_EXISTS:
            return "Entry already exists";
        case VFS_ERR_NOT_DIR:
            return "Component is not a directory";
        case VFS_ERR_IS_DIR:
            return "Target is a directory";
        case VFS_ERR_NO_MEM:
            return "Out of kernel heap memory";
        case VFS_ERR_NAME_TOO_LONG:
            return "Component name exceeds limit";
        case VFS_ERR_PATH_TOO_LONG:
            return "Absolute path exceeds limit";
        case VFS_ERR_NOT_SUPPORTED:
            return "Operation not supported";
        case VFS_ERR_IO:
            return "I/O or internal VFS error";
        default:
            return "Unknown error";
    }
}

/*
 * vfs_run_tests - In-kernel test harness validating VFS and RAMFS semantics.
 * Outputs exact test status strings required by Stage 11A.
 */
int vfs_run_tests(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));

    /* 1. VFS Initialization */
    if (vfs_root_node == NULL) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] VFS root not initialized\n");
        return -1;
    }
    vga_puts("[OK] VFS initialized\n");

    /* 2. RAMFS mount at / */
    if (vfs_root_fs == NULL || kstrcmp(vfs_root_fs->name, "ramfs") != 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] RAMFS not mounted at /\n");
        return -1;
    }
    vga_puts("[OK] RAMFS mounted at /\n");

    /* 3. Root lookup */
    vfs_node_t *node = NULL;
    int err = vfs_lookup("/", &node);
    if (err != VFS_OK || node != vfs_root_node || node->type != VFS_NODE_DIRECTORY) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Root lookup failed\n");
        return -1;
    }
    vga_puts("[OK] Root lookup\n");

    /* 4. File lookup (/readme.txt, /bin, /etc) */
    vfs_node_t *readme_node = NULL;
    err = vfs_lookup("/readme.txt", &readme_node);
    if (err != VFS_OK || readme_node == NULL || readme_node->type != VFS_NODE_FILE || readme_node->size != 23) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] File lookup /readme.txt failed\n");
        return -1;
    }
    vfs_node_t *bin_node = NULL;
    err = vfs_lookup("/bin", &bin_node);
    if (err != VFS_OK || bin_node == NULL || bin_node->type != VFS_NODE_DIRECTORY) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Directory lookup /bin failed\n");
        return -1;
    }
    vfs_node_t *etc_node = NULL;
    err = vfs_lookup("/etc", &etc_node);
    if (err != VFS_OK || etc_node == NULL || etc_node->type != VFS_NODE_DIRECTORY) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Directory lookup /etc failed\n");
        return -1;
    }
    vga_puts("[OK] File lookup\n");

    /* 5. File read */
    char buf[64];
    size_t bytes = 0;
    err = vfs_read(readme_node, buf, 0, 23, &bytes);
    if (err != VFS_OK || bytes != 23 || kstrncmp(buf, "Hello from MyOS RAMFS!\n", 23) != 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Reading /readme.txt failed\n");
        return -1;
    }
    /* Zero-length read */
    err = vfs_read(readme_node, buf, 0, 0, &bytes);
    if (err != VFS_OK || bytes != 0) {
        vga_puts("[FAIL] Zero-length read failed\n");
        return -1;
    }
    /* Read at EOF */
    err = vfs_read(readme_node, buf, 23, 10, &bytes);
    if (err != VFS_OK || bytes != 0) {
        vga_puts("[FAIL] EOF read failed\n");
        return -1;
    }
    /* Read beyond EOF */
    err = vfs_read(readme_node, buf, 100, 10, &bytes);
    if (err != VFS_OK || bytes != 0) {
        vga_puts("[FAIL] Beyond-EOF read failed\n");
        return -1;
    }
    /* Read on directory */
    err = vfs_read(bin_node, buf, 0, 10, &bytes);
    if (err != VFS_ERR_IS_DIR) {
        vga_puts("[FAIL] Read on directory should return VFS_ERR_IS_DIR\n");
        return -1;
    }
    vga_puts("[OK] File read\n");

    /* 6. File creation & duplicate rejection */
    vfs_node_t *new_file = NULL;
    err = vfs_create("/test_file.txt", &new_file);
    if (err != VFS_OK || new_file == NULL || new_file->type != VFS_NODE_FILE || new_file->size != 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Creating /test_file.txt failed\n");
        return -1;
    }
    /* Duplicate file rejection */
    err = vfs_create("/test_file.txt", &new_file);
    if (err != VFS_ERR_EXISTS) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Duplicate file rejection failed\n");
        return -1;
    }
    /* Creation under non-existent directory */
    err = vfs_create("/nonexistent/file.txt", &new_file);
    if (err != VFS_ERR_NOT_FOUND) {
        vga_puts("[FAIL] Creation under non-existent directory should return VFS_ERR_NOT_FOUND\n");
        return -1;
    }
    /* Creation under regular file */
    err = vfs_create("/readme.txt/bad.txt", &new_file);
    if (err != VFS_ERR_NOT_DIR) {
        vga_puts("[FAIL] Creation under regular file should return VFS_ERR_NOT_DIR\n");
        return -1;
    }
    vga_puts("[OK] File creation\n");

    /* 7. Directory creation */
    vfs_node_t *new_dir = NULL;
    err = vfs_mkdir("/test_dir", &new_dir);
    if (err != VFS_OK || new_dir == NULL || new_dir->type != VFS_NODE_DIRECTORY) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Creating /test_dir failed\n");
        return -1;
    }
    /* Duplicate directory rejection */
    err = vfs_mkdir("/test_dir", &new_dir);
    if (err != VFS_ERR_EXISTS) {
        vga_puts("[FAIL] Duplicate directory rejection failed\n");
        return -1;
    }
    /* Nested directory creation */
    vfs_node_t *sub1 = NULL, *sub2 = NULL, *sub3 = NULL;
    err = vfs_mkdir("/docs", &sub1);
    if (err != VFS_OK) { vga_puts("[FAIL] mkdir /docs failed\n"); return -1; }
    err = vfs_mkdir("/docs/os", &sub2);
    if (err != VFS_OK) { vga_puts("[FAIL] mkdir /docs/os failed\n"); return -1; }
    err = vfs_mkdir("/docs/os/stage11", &sub3);
    if (err != VFS_OK) { vga_puts("[FAIL] mkdir /docs/os/stage11 failed\n"); return -1; }

    /* Verify nested path lookup */
    vfs_node_t *nested_lookup = NULL;
    err = vfs_lookup("/docs/os/stage11", &nested_lookup);
    if (err != VFS_OK || nested_lookup != sub3) {
        vga_puts("[FAIL] Lookup nested directory failed\n");
        return -1;
    }
    vga_puts("[OK] Directory creation\n");

    /* 8. File write/read */
    const char *payload = "Hello World! 1234567";
    size_t written = 0;
    err = vfs_write(new_file, payload, 0, 20, &written);
    if (err != VFS_OK || written != 20 || new_file->size != 20) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Writing to /test_file.txt failed\n");
        return -1;
    }
    char verify_buf[32];
    err = vfs_read(new_file, verify_buf, 0, 20, &bytes);
    if (err != VFS_OK || bytes != 20 || kstrncmp(verify_buf, payload, 20) != 0) {
        vga_puts("[FAIL] Reading back written data failed\n");
        return -1;
    }
    /* Overwrite partial data: replace "World!" (6 chars at offset 6) with "Kernel" (6 chars) */
    err = vfs_write(new_file, "Kernel", 6, 6, &written);
    if (err != VFS_OK || written != 6 || new_file->size != 20) {
        vga_puts("[FAIL] Overwriting data failed\n");
        return -1;
    }
    err = vfs_read(new_file, verify_buf, 0, 20, &bytes);
    if (err != VFS_OK || bytes != 20 || kstrncmp(verify_buf, "Hello Kernel 1234567", 20) != 0) {
        vga_puts("[FAIL] Verifying overwritten data failed\n");
        return -1;
    }
    /* File growth */
    err = vfs_write(new_file, " Stage 11A", 20, 10, &written);
    if (err != VFS_OK || written != 10 || new_file->size != 30) {
        vga_puts("[FAIL] File expansion failed\n");
        return -1;
    }
    /* Sparse write rejection */
    err = vfs_write(new_file, "Gap", 100, 3, &written);
    if (err != VFS_ERR_NOT_SUPPORTED) {
        vga_puts("[FAIL] Sparse gap write should return VFS_ERR_NOT_SUPPORTED\n");
        return -1;
    }
    /* Write on directory */
    err = vfs_write(new_dir, "Invalid", 0, 7, &written);
    if (err != VFS_ERR_IS_DIR) {
        vga_puts("[FAIL] Write on directory should return VFS_ERR_IS_DIR\n");
        return -1;
    }
    vga_puts("[OK] File write/read\n");

    /* 9. Path validation */
    /* Relative path rejection */
    err = vfs_lookup("readme.txt", &node);
    if (err != VFS_ERR_INVALID) { vga_puts("[FAIL] Relative path not rejected\n"); return -1; }
    /* Empty path rejection */
    err = vfs_lookup("", &node);
    if (err != VFS_ERR_INVALID) { vga_puts("[FAIL] Empty path not rejected\n"); return -1; }
    /* Null path rejection */
    err = vfs_lookup(NULL, &node);
    if (err != VFS_ERR_INVALID) { vga_puts("[FAIL] NULL path not rejected\n"); return -1; }
    /* Redundant slashes */
    err = vfs_lookup("///readme.txt", &node);
    if (err != VFS_OK || node != readme_node) { vga_puts("[FAIL] Redundant slashes lookup failed\n"); return -1; }
    err = vfs_lookup("///bin///", &node);
    if (err != VFS_OK || node != bin_node) { vga_puts("[FAIL] Redundant slashes on dir failed\n"); return -1; }
    /* Nonexistent path */
    err = vfs_lookup("/missing_file.txt", &node);
    if (err != VFS_ERR_NOT_FOUND) { vga_puts("[FAIL] Missing file should return VFS_ERR_NOT_FOUND\n"); return -1; }
    /* Traversal through regular file */
    err = vfs_lookup("/readme.txt/subfile", &node);
    if (err != VFS_ERR_NOT_DIR) { vga_puts("[FAIL] Traversal through regular file should return VFS_ERR_NOT_DIR\n"); return -1; }
    /* Component name too long */
    err = vfs_lookup("/this_component_name_exceeds_maximum_vfs_limit", &node);
    if (err != VFS_ERR_NAME_TOO_LONG) { vga_puts("[FAIL] Long component not rejected\n"); return -1; }
    vga_puts("[OK] Path validation\n");

    /* 10. Memory stability */
    heap_stats_t stats;
    heap_get_stats(&stats);
    if (stats.used_bytes >= HEAP_SIZE || stats.free_bytes == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Heap memory exhausted\n");
        return -1;
    }
    vga_puts("[OK] Memory stability\n");

    return 0;
}
