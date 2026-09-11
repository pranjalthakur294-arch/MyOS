#include "ramfs.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Freestanding memory and string utilities
 */
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

/*
 * RAMFS Internal Directory Entry
 */
typedef struct ramfs_dirent {
    char name[VFS_NAME_MAX];
    vfs_node_t *node;
    bool is_static;
    struct ramfs_dirent *next;
} ramfs_dirent_t;

/*
 * RAMFS Node Structure
 * Wraps vfs_node_t as first member for direct reference.
 */
typedef struct ramfs_node {
    vfs_node_t vfs_node;
    bool is_static;
    union {
        struct {
            ramfs_dirent_t *children_head;
        } dir;
        struct {
            uint8_t *data;
            size_t capacity;
            bool data_is_static;
        } file;
    };
} ramfs_node_t;

/* Forward declarations of RAMFS operations */
static int ramfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read);
static int ramfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written);
static int ramfs_lookup(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
static int ramfs_create(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
static int ramfs_mkdir(vfs_node_t *dir, const char *name, vfs_node_t **out_node);
static int ramfs_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent);

static vfs_node_ops_t ramfs_node_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .readdir = ramfs_readdir
};

/*
 * Static Initial RAMFS Nodes and Directory Entries
 * Statically initialized so the kernel heap remains 100% pristine at boot (0 used bytes,
 * 65512 free bytes) ensuring complete backward compatibility with Stage 6 heap tests.
 */
static vfs_fs_t static_fs;
static ramfs_node_t static_root;
static ramfs_node_t static_bin;
static ramfs_node_t static_etc;
static ramfs_node_t static_readme;

/* Embedded user-space ELF image symbols exported by elf_image.S */
extern const uint8_t _binary_test_program_elf_start[];
extern const uint8_t _binary_test_program_elf_end[];
extern const uint64_t _binary_test_program_elf_size;

static ramfs_node_t static_bin_test;
static ramfs_dirent_t static_dirent_bin_test;

static ramfs_node_t static_bin_bad;
static ramfs_dirent_t static_dirent_bin_bad;
static uint8_t static_bad_elf_content[32] = "NOT_AN_ELF_FILE\n";

static ramfs_dirent_t static_dirent_bin;
static ramfs_dirent_t static_dirent_etc;
static ramfs_dirent_t static_dirent_readme;

static uint8_t static_readme_content[32] = "Hello from MyOS RAMFS!\n";

/*
 * ramfs_read - Reads data from a RAMFS regular file.
 */
static int ramfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read) {
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
    ramfs_node_t *rnode = (ramfs_node_t *)node->internal_data;
    if (rnode == NULL) {
        return VFS_ERR_IO;
    }

    if (offset >= node->size) {
        *bytes_read = 0;
        return VFS_OK;
    }

    uint64_t avail = node->size - offset;
    size_t to_read = (size < avail) ? size : (size_t)avail;

    if (to_read > 0 && rnode->file.data != NULL) {
        kmemcpy(buffer, rnode->file.data + offset, to_read);
    }
    *bytes_read = to_read;
    return VFS_OK;
}

/*
 * ramfs_write - Writes data into a RAMFS regular file.
 * Non-sparse policy: rejects gap writes (offset > size) with VFS_ERR_NOT_SUPPORTED.
 */
static int ramfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written) {
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
    ramfs_node_t *rnode = (ramfs_node_t *)node->internal_data;
    if (rnode == NULL) {
        return VFS_ERR_IO;
    }

    /* Integer overflow protection on offset + size */
    if (offset > (uint64_t)-1 - size) {
        return VFS_ERR_INVALID;
    }
    uint64_t end_pos = offset + size;

    /* Guard against exceeding reasonable RAMFS file limits on 64 KiB heap */
    if (end_pos > 32768ULL) {
        return VFS_ERR_NO_MEM;
    }

    /* Non-sparse write policy: reject writes that leave an uninitialized gap */
    if (offset > node->size) {
        return VFS_ERR_NOT_SUPPORTED;
    }

    /* Expand buffer capacity if needed */
    if (end_pos > rnode->file.capacity || rnode->file.data_is_static) {
        size_t new_cap = (size_t)((end_pos + 63ULL) & ~63ULL);
        if (new_cap < 64) {
            new_cap = 64;
        }
        uint8_t *new_buf = (uint8_t *)kmalloc(new_cap);
        if (new_buf == NULL) {
            return VFS_ERR_NO_MEM;
        }
        if (rnode->file.data != NULL && node->size > 0) {
            kmemcpy(new_buf, rnode->file.data, (size_t)node->size);
        }
        if (new_cap > node->size) {
            kmemset(new_buf + node->size, 0, new_cap - (size_t)node->size);
        }
        if (rnode->file.data != NULL && !rnode->file.data_is_static) {
            kfree(rnode->file.data);
        }
        rnode->file.data = new_buf;
        rnode->file.capacity = new_cap;
        rnode->file.data_is_static = false;
    }

    /* Write data at target offset */
    kmemcpy(rnode->file.data + offset, buffer, size);
    if (end_pos > node->size) {
        node->size = end_pos;
    }
    *bytes_written = size;
    return VFS_OK;
}

/*
 * ramfs_lookup - Looks up a direct child by name in a directory.
 */
static int ramfs_lookup(vfs_node_t *dir, const char *name, vfs_node_t **out_node) {
    if (dir == NULL || name == NULL || out_node == NULL) {
        return VFS_ERR_INVALID;
    }
    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    ramfs_node_t *rdir = (ramfs_node_t *)dir->internal_data;
    if (rdir == NULL) {
        return VFS_ERR_IO;
    }

    ramfs_dirent_t *curr = rdir->dir.children_head;
    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            *out_node = curr->node;
            return VFS_OK;
        }
        curr = curr->next;
    }
    return VFS_ERR_NOT_FOUND;
}

/*
 * ramfs_create - Creates a regular file child inside a directory.
 */
static int ramfs_create(vfs_node_t *dir, const char *name, vfs_node_t **out_node) {
    if (dir == NULL || name == NULL) {
        return VFS_ERR_INVALID;
    }
    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    size_t nlen = kstrlen(name);
    if (nlen == 0 || nlen >= VFS_NAME_MAX) {
        return (nlen >= VFS_NAME_MAX) ? VFS_ERR_NAME_TOO_LONG : VFS_ERR_INVALID;
    }
    ramfs_node_t *rdir = (ramfs_node_t *)dir->internal_data;
    if (rdir == NULL) {
        return VFS_ERR_IO;
    }

    /* Duplicate rejection */
    ramfs_dirent_t *curr = rdir->dir.children_head;
    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            return VFS_ERR_EXISTS;
        }
        curr = curr->next;
    }

    /* Allocate node and dirent with clean rollback on failure */
    ramfs_node_t *rnode = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (rnode == NULL) {
        return VFS_ERR_NO_MEM;
    }
    ramfs_dirent_t *dirent = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (dirent == NULL) {
        kfree(rnode);
        return VFS_ERR_NO_MEM;
    }

    kstrncpy(rnode->vfs_node.name, name, VFS_NAME_MAX);
    rnode->vfs_node.type = VFS_NODE_FILE;
    rnode->vfs_node.size = 0;
    rnode->vfs_node.permissions = 0644;
    rnode->vfs_node.ops = &ramfs_node_ops;
    rnode->vfs_node.fs = dir->fs;
    rnode->vfs_node.internal_data = rnode;
    rnode->is_static = false;
    rnode->file.data = NULL;
    rnode->file.capacity = 0;
    rnode->file.data_is_static = false;

    kstrncpy(dirent->name, name, VFS_NAME_MAX);
    dirent->node = &rnode->vfs_node;
    dirent->is_static = false;
    dirent->next = rdir->dir.children_head;
    rdir->dir.children_head = dirent;

    if (out_node != NULL) {
        *out_node = &rnode->vfs_node;
    }
    return VFS_OK;
}

/*
 * ramfs_mkdir - Creates a directory child inside a directory.
 */
static int ramfs_mkdir(vfs_node_t *dir, const char *name, vfs_node_t **out_node) {
    if (dir == NULL || name == NULL) {
        return VFS_ERR_INVALID;
    }
    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    size_t nlen = kstrlen(name);
    if (nlen == 0 || nlen >= VFS_NAME_MAX) {
        return (nlen >= VFS_NAME_MAX) ? VFS_ERR_NAME_TOO_LONG : VFS_ERR_INVALID;
    }
    ramfs_node_t *rdir = (ramfs_node_t *)dir->internal_data;
    if (rdir == NULL) {
        return VFS_ERR_IO;
    }

    /* Duplicate rejection */
    ramfs_dirent_t *curr = rdir->dir.children_head;
    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            return VFS_ERR_EXISTS;
        }
        curr = curr->next;
    }

    /* Allocate directory node and dirent with clean rollback on failure */
    ramfs_node_t *rnode = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (rnode == NULL) {
        return VFS_ERR_NO_MEM;
    }
    ramfs_dirent_t *dirent = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (dirent == NULL) {
        kfree(rnode);
        return VFS_ERR_NO_MEM;
    }

    kstrncpy(rnode->vfs_node.name, name, VFS_NAME_MAX);
    rnode->vfs_node.type = VFS_NODE_DIRECTORY;
    rnode->vfs_node.size = 0;
    rnode->vfs_node.permissions = 0755;
    rnode->vfs_node.ops = &ramfs_node_ops;
    rnode->vfs_node.fs = dir->fs;
    rnode->vfs_node.internal_data = rnode;
    rnode->is_static = false;
    rnode->dir.children_head = NULL;

    kstrncpy(dirent->name, name, VFS_NAME_MAX);
    dirent->node = &rnode->vfs_node;
    dirent->is_static = false;
    dirent->next = rdir->dir.children_head;
    rdir->dir.children_head = dirent;

    if (out_node != NULL) {
        *out_node = &rnode->vfs_node;
    }
    return VFS_OK;
}

/*
 * ramfs_readdir - Iterates over children of a RAMFS directory node.
 * Returns VFS_OK on success, VFS_EOF at end of directory, or negative error.
 */
static int ramfs_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *dirent) {
    if (dir == NULL || dirent == NULL) {
        return VFS_ERR_INVALID;
    }
    if (dir->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_NOT_DIR;
    }
    ramfs_node_t *rdir = (ramfs_node_t *)dir->internal_data;
    if (rdir == NULL) {
        return VFS_ERR_IO;
    }

    ramfs_dirent_t *curr = rdir->dir.children_head;
    uint64_t i = 0;
    while (curr != NULL) {
        if (i == index) {
            kstrncpy(dirent->name, curr->name, VFS_NAME_MAX);
            if (curr->node != NULL) {
                dirent->type = curr->node->type;
                dirent->size = curr->node->size;
            } else {
                dirent->type = VFS_NODE_INVALID;
                dirent->size = 0;
            }
            return VFS_OK;
        }
        curr = curr->next;
        i++;
    }

    return VFS_EOF;
}

/*
 * ramfs_destroy_node - Recursively frees all heap memory owned by a RAMFS node.
 * Strictly avoids freeing static/global memory as required by specification.
 */
static void ramfs_destroy_node(vfs_node_t *node) {
    if (node == NULL) {
        return;
    }
    ramfs_node_t *rnode = (ramfs_node_t *)node->internal_data;
    if (rnode == NULL) {
        return;
    }

    if (node->type == VFS_NODE_DIRECTORY) {
        ramfs_dirent_t *curr = rnode->dir.children_head;
        while (curr != NULL) {
            ramfs_dirent_t *next = curr->next;
            if (curr->node != NULL) {
                ramfs_destroy_node(curr->node);
            }
            if (!curr->is_static) {
                kfree(curr);
            }
            curr = next;
        }
    } else if (node->type == VFS_NODE_FILE) {
        if (rnode->file.data != NULL && !rnode->file.data_is_static) {
            kfree(rnode->file.data);
            rnode->file.data = NULL;
        }
    }

    if (!rnode->is_static) {
        kfree(rnode);
    }
}

/*
 * ramfs_unmount - Unmounts and tears down the RAMFS filesystem.
 */
static int ramfs_unmount(vfs_fs_t *fs) {
    if (fs == NULL) {
        return VFS_ERR_INVALID;
    }
    if (fs->root != NULL) {
        ramfs_destroy_node(fs->root);
        fs->root = NULL;
    }
    if (fs != &static_fs) {
        kfree(fs);
    }
    return VFS_OK;
}

static vfs_fs_ops_t ramfs_fs_ops = {
    .unmount = ramfs_unmount
};

/*
 * ramfs_create_fs - Instantiates a new RAMFS filesystem and populates initial files.
 * Uses static structures for initial root image to preserve heap state for Stage 6 tests.
 */
vfs_fs_t *ramfs_create_fs(void) {
    /* Initialize static root node */
    kstrncpy(static_root.vfs_node.name, "/", VFS_NAME_MAX);
    static_root.vfs_node.type = VFS_NODE_DIRECTORY;
    static_root.vfs_node.size = 0;
    static_root.vfs_node.permissions = 0755;
    static_root.vfs_node.ops = &ramfs_node_ops;
    static_root.vfs_node.fs = &static_fs;
    static_root.vfs_node.internal_data = &static_root;
    static_root.is_static = true;
    static_root.dir.children_head = NULL;

    /* Initialize static /bin directory */
    kstrncpy(static_bin.vfs_node.name, "bin", VFS_NAME_MAX);
    static_bin.vfs_node.type = VFS_NODE_DIRECTORY;
    static_bin.vfs_node.size = 0;
    static_bin.vfs_node.permissions = 0755;
    static_bin.vfs_node.ops = &ramfs_node_ops;
    static_bin.vfs_node.fs = &static_fs;
    static_bin.vfs_node.internal_data = &static_bin;
    static_bin.is_static = true;

    /* Initialize static /bin/test executable */
    size_t test_elf_size = (size_t)(_binary_test_program_elf_end - _binary_test_program_elf_start);
    kstrncpy(static_bin_test.vfs_node.name, "test", VFS_NAME_MAX);
    static_bin_test.vfs_node.type = VFS_NODE_FILE;
    static_bin_test.vfs_node.size = test_elf_size;
    static_bin_test.vfs_node.permissions = 0755;
    static_bin_test.vfs_node.ops = &ramfs_node_ops;
    static_bin_test.vfs_node.fs = &static_fs;
    static_bin_test.vfs_node.internal_data = &static_bin_test;
    static_bin_test.is_static = true;
    static_bin_test.file.data = (uint8_t *)_binary_test_program_elf_start;
    static_bin_test.file.capacity = test_elf_size;
    static_bin_test.file.data_is_static = true;

    /* Initialize static /bin/bad corrupted ELF file for error handling verification */
    kstrncpy(static_bin_bad.vfs_node.name, "bad", VFS_NAME_MAX);
    static_bin_bad.vfs_node.type = VFS_NODE_FILE;
    static_bin_bad.vfs_node.size = 16;
    static_bin_bad.vfs_node.permissions = 0644;
    static_bin_bad.vfs_node.ops = &ramfs_node_ops;
    static_bin_bad.vfs_node.fs = &static_fs;
    static_bin_bad.vfs_node.internal_data = &static_bin_bad;
    static_bin_bad.is_static = true;
    static_bin_bad.file.data = static_bad_elf_content;
    static_bin_bad.file.capacity = sizeof(static_bad_elf_content);
    static_bin_bad.file.data_is_static = true;

    /* Link entries into /bin: /bin/test -> /bin/bad -> NULL */
    kstrncpy(static_dirent_bin_bad.name, "bad", VFS_NAME_MAX);
    static_dirent_bin_bad.node = &static_bin_bad.vfs_node;
    static_dirent_bin_bad.is_static = true;
    static_dirent_bin_bad.next = NULL;

    kstrncpy(static_dirent_bin_test.name, "test", VFS_NAME_MAX);
    static_dirent_bin_test.node = &static_bin_test.vfs_node;
    static_dirent_bin_test.is_static = true;
    static_dirent_bin_test.next = &static_dirent_bin_bad;

    static_bin.dir.children_head = &static_dirent_bin_test;

    /* Initialize static /etc directory */
    kstrncpy(static_etc.vfs_node.name, "etc", VFS_NAME_MAX);
    static_etc.vfs_node.type = VFS_NODE_DIRECTORY;
    static_etc.vfs_node.size = 0;
    static_etc.vfs_node.permissions = 0755;
    static_etc.vfs_node.ops = &ramfs_node_ops;
    static_etc.vfs_node.fs = &static_fs;
    static_etc.vfs_node.internal_data = &static_etc;
    static_etc.is_static = true;
    static_etc.dir.children_head = NULL;

    /* Initialize static /readme.txt file */
    kstrncpy(static_readme.vfs_node.name, "readme.txt", VFS_NAME_MAX);
    static_readme.vfs_node.type = VFS_NODE_FILE;
    static_readme.vfs_node.size = 23;
    static_readme.vfs_node.permissions = 0644;
    static_readme.vfs_node.ops = &ramfs_node_ops;
    static_readme.vfs_node.fs = &static_fs;
    static_readme.vfs_node.internal_data = &static_readme;
    static_readme.is_static = true;
    static_readme.file.data = static_readme_content;
    static_readme.file.capacity = sizeof(static_readme_content);
    static_readme.file.data_is_static = true;

    /* Link directory entries into root directory */
    kstrncpy(static_dirent_bin.name, "bin", VFS_NAME_MAX);
    static_dirent_bin.node = &static_bin.vfs_node;
    static_dirent_bin.is_static = true;
    static_dirent_bin.next = NULL;

    kstrncpy(static_dirent_etc.name, "etc", VFS_NAME_MAX);
    static_dirent_etc.node = &static_etc.vfs_node;
    static_dirent_etc.is_static = true;
    static_dirent_etc.next = &static_dirent_bin;

    kstrncpy(static_dirent_readme.name, "readme.txt", VFS_NAME_MAX);
    static_dirent_readme.node = &static_readme.vfs_node;
    static_dirent_readme.is_static = true;
    static_dirent_readme.next = &static_dirent_etc;

    static_root.dir.children_head = &static_dirent_readme;

    /* Initialize static filesystem object */
    static_fs.name = "ramfs";
    static_fs.root = &static_root.vfs_node;
    static_fs.ops = &ramfs_fs_ops;
    static_fs.internal_data = &static_root;

    return &static_fs;
}

/*
 * ramfs_destroy_fs - Destroys a RAMFS filesystem and frees all its allocated resources.
 */
void ramfs_destroy_fs(vfs_fs_t *fs) {
    if (fs != NULL && fs->ops != NULL && fs->ops->unmount != NULL) {
        fs->ops->unmount(fs);
    }
}
