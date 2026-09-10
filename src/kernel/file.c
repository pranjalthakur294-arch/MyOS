/*
 * file.c - File Descriptor and Open-File Table Subsystem Implementation (Stage 11B)
 *
 * Connects process file descriptors to open-file objects and the VFS core layer.
 */

#include "file.h"
#include "process.h"
#include "syscall.h"
#include "heap.h"
#include "vga.h"
#include "user.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Static Console Stream Objects (stdin, stdout, stderr)
 * Pre-allocated to avoid consuming heap memory at boot or process creation.
 */
static open_file_t console_stdin = {
    .type = OPEN_FILE_CONSOLE,
    .node = NULL,
    .offset = 0,
    .flags = O_RDONLY,
    .refcount = 1,
    .is_static = true
};

static open_file_t console_stdout = {
    .type = OPEN_FILE_CONSOLE,
    .node = NULL,
    .offset = 0,
    .flags = O_WRONLY,
    .refcount = 1,
    .is_static = true
};

static open_file_t console_stderr = {
    .type = OPEN_FILE_CONSOLE,
    .node = NULL,
    .offset = 0,
    .flags = O_WRONLY,
    .refcount = 1,
    .is_static = true
};

/*
 * file_init - Initializes the file descriptor subsystem.
 * Quiet initialization (0 screen rows) to preserve screen line budget.
 */
void file_init(void) {
    /* Confirm static console stream objects are ready */
    console_stdin.refcount = 1;
    console_stdout.refcount = 1;
    console_stderr.refcount = 1;
}

/*
 * fd_init_process - Initializes a process's FD table with standard streams.
 * Sets fds[0] = stdin, fds[1] = stdout, fds[2] = stderr, and fds[3..MAX-1] = NULL.
 */
void fd_init_process(void *proc_ptr) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc) {
        return;
    }

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        proc->fds[i] = NULL;
    }

    /* Standard streams 0, 1, 2 */
    proc->fds[0] = &console_stdin;
    proc->fds[1] = &console_stdout;
    proc->fds[2] = &console_stderr;
}

/*
 * fd_open - Resolves a path via VFS and allocates the lowest available FD.
 *
 * Parameters:
 *   proc_ptr - Owning process structure.
 *   path     - Absolute path to target file/directory.
 *   flags    - Access mode (O_RDONLY, O_WRONLY, O_RDWR).
 *
 * Returns:
 *   Allocated file descriptor (int >= 0) on success.
 *   Negative error code on failure.
 */
int fd_open(void *proc_ptr, const char *path, uint32_t flags) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || !path) {
        return SYSCALL_EINVAL;
    }

    /* Validate flags */
    if (!(flags & (O_RDONLY | O_WRONLY))) {
        return SYSCALL_EINVAL;
    }
    if ((flags & ~O_RDWR) != 0) {
        return SYSCALL_EINVAL;
    }

    /* Locate the lowest available FD slot */
    int fd = -1;
    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (proc->fds[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return SYSCALL_EMFILE;
    }

    /* Resolve path through VFS abstraction */
    vfs_node_t *node = NULL;
    int vfs_err = vfs_lookup(path, &node);
    if (vfs_err != VFS_OK) {
        switch (vfs_err) {
            case VFS_ERR_NOT_FOUND:
                return SYSCALL_ENOENT;
            case VFS_ERR_NOT_DIR:
                return SYSCALL_ENOENT;
            case VFS_ERR_PATH_TOO_LONG:
            case VFS_ERR_NAME_TOO_LONG:
            case VFS_ERR_INVALID:
                return SYSCALL_EINVAL;
            default:
                return SYSCALL_EINVAL;
        }
    }

    /* Allocate independent open_file object */
    open_file_t *of = (open_file_t *)kmalloc(sizeof(open_file_t));
    if (!of) {
        return SYSCALL_ENOMEM;
    }

    of->type = OPEN_FILE_VFS;
    of->node = node;
    of->offset = 0;
    of->flags = flags;
    of->refcount = 1;
    of->is_static = false;

    proc->fds[fd] = of;
    return fd;
}

/*
 * fd_read - Reads data from an open file starting at its current offset.
 * Advances the offset by the number of bytes successfully read.
 */
int64_t fd_read(void *proc_ptr, int fd, void *buf, size_t count) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS) {
        return SYSCALL_EBADF;
    }

    open_file_t *of = proc->fds[fd];
    if (!of) {
        return SYSCALL_EBADF;
    }

    /* Enforce read permission */
    if (!(of->flags & O_RDONLY)) {
        return SYSCALL_EACCES;
    }

    if (count == 0) {
        return 0;
    }

    if (!buf) {
        return SYSCALL_EFAULT;
    }

    /* Console stdin stream */
    if (of->type == OPEN_FILE_CONSOLE) {
        return 0; /* EOF for educational non-interactive stdin read */
    }

    if (of->type != OPEN_FILE_VFS || !of->node) {
        return SYSCALL_EBADF;
    }

    /* Reject directory reading through regular read */
    if (of->node->type == VFS_NODE_DIRECTORY) {
        return SYSCALL_EISDIR;
    }

    size_t bytes_read = 0;
    int err = vfs_read(of->node, buf, of->offset, count, &bytes_read);
    if (err != VFS_OK) {
        switch (err) {
            case VFS_ERR_IS_DIR:
                return SYSCALL_EISDIR;
            default:
                return SYSCALL_EINVAL;
        }
    }

    /* Advance open-file offset by actual bytes read */
    of->offset += bytes_read;
    return (int64_t)bytes_read;
}

/*
 * fd_write - Writes data into an open file starting at its current offset.
 * Advances the offset by the number of bytes successfully written.
 */
int64_t fd_write(void *proc_ptr, int fd, const void *buf, size_t count) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS) {
        return SYSCALL_EBADF;
    }

    open_file_t *of = proc->fds[fd];
    if (!of) {
        return SYSCALL_EBADF;
    }

    /* Enforce write permission */
    if (!(of->flags & O_WRONLY)) {
        return SYSCALL_EACCES;
    }

    if (count == 0) {
        return 0;
    }

    if (!buf) {
        return SYSCALL_EFAULT;
    }

    /* Console stdout/stderr stream */
    if (of->type == OPEN_FILE_CONSOLE) {
        const char *cbuf = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            vga_putc(cbuf[i]);
        }
        return (int64_t)count;
    }

    if (of->type != OPEN_FILE_VFS || !of->node) {
        return SYSCALL_EBADF;
    }

    /* Reject directory writing */
    if (of->node->type == VFS_NODE_DIRECTORY) {
        return SYSCALL_EISDIR;
    }

    size_t bytes_written = 0;
    int err = vfs_write(of->node, buf, of->offset, count, &bytes_written);
    if (err != VFS_OK) {
        /* On write failure (e.g. non-sparse gap write), do NOT advance offset */
        switch (err) {
            case VFS_ERR_NOT_SUPPORTED:
                return SYSCALL_ENOTSUP;
            case VFS_ERR_IS_DIR:
                return SYSCALL_EISDIR;
            case VFS_ERR_NO_MEM:
                return SYSCALL_ENOMEM;
            default:
                return SYSCALL_EINVAL;
        }
    }

    /* Advance open-file offset by actual bytes written */
    of->offset += bytes_written;
    return (int64_t)bytes_written;
}

/*
 * fd_close - Closes an open file descriptor and frees the open-file object.
 * Clears the FD table slot to allow immediate reuse.
 */
int fd_close(void *proc_ptr, int fd) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS) {
        return SYSCALL_EBADF;
    }

    open_file_t *of = proc->fds[fd];
    if (!of) {
        return SYSCALL_EBADF;
    }

    /* Clear the process FD slot so subsequent opens can reuse it */
    proc->fds[fd] = NULL;

    if (of->refcount > 0) {
        of->refcount--;
    }

    /* Free dynamically allocated open_file objects */
    if (of->refcount == 0 && !of->is_static) {
        kfree(of);
    }

    return SYSCALL_SUCCESS;
}

/*
 * fd_get_size - Retrieves the total file size in bytes for an open file descriptor.
 * Returns file size on success, 0 for character streams, or negative error code on invalid FD.
 */
int64_t fd_get_size(void *proc_ptr, int fd) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS) {
        return SYSCALL_EBADF;
    }

    open_file_t *of = proc->fds[fd];
    if (!of) {
        return SYSCALL_EBADF;
    }

    if (of->type == OPEN_FILE_VFS && of->node != NULL) {
        return (int64_t)of->node->size;
    } else if (of->type == OPEN_FILE_CONSOLE) {
        return 0;
    }

    return SYSCALL_EBADF;
}

/*
 * fd_close_all - Closes and cleans up all open file descriptors for a process.
 * Invoked during process termination and deferred reaping.
 */
void fd_close_all(void *proc_ptr) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc) {
        return;
    }

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (proc->fds[i] != NULL) {
            open_file_t *of = proc->fds[i];
            proc->fds[i] = NULL;
            if (of->refcount > 0) {
                of->refcount--;
            }
            if (of->refcount == 0 && !of->is_static) {
                kfree(of);
            }
        }
    }
}

/*
 * fd_run_tests - In-kernel test harness validating FD subsystem mechanics.
 */
int fd_run_tests(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Running Stage 11B File Descriptor verification suite...\n");

    /* Create dummy process context for kernel test */
    process_t test_proc;
    fd_init_process(&test_proc);

    /* 1. Verify standard streams initialized */
    if (!test_proc.fds[0] || !test_proc.fds[1] || !test_proc.fds[2]) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Standard streams not initialized\n");
        return -1;
    }
    vga_puts("[OK] Standard streams (stdin, stdout, stderr)\n");

    /* 2. Open /readme.txt (should get lowest available: fd 3) */
    int fd1 = fd_open(&test_proc, "/readme.txt", O_RDONLY);
    if (fd1 != 3) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] fd_open expected fd 3\n");
        return -1;
    }
    vga_puts("[OK] File open (/readme.txt -> fd 3)\n");

    /* 3. Read from fd 3 */
    char buf[32];
    int64_t nread = fd_read(&test_proc, fd1, buf, 10);
    if (nread != 10 || buf[0] != 'H' || test_proc.fds[fd1]->offset != 10) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] fd_read failed\n");
        return -1;
    }
    vga_puts("[OK] File read and offset advance\n");

    /* 4. Open /readme.txt second time (should get fd 4 with independent offset) */
    int fd2 = fd_open(&test_proc, "/readme.txt", O_RDONLY);
    if (fd2 != 4 || test_proc.fds[fd2]->offset != 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Second open independent offset failed\n");
        return -1;
    }
    if (test_proc.fds[fd1]->offset != 10) {
        vga_puts("[FAIL] First open offset corrupted\n");
        return -1;
    }
    vga_puts("[OK] Independent open-file offsets\n");

    /* 5. Close fd 3 and verify slot is cleared */
    int close_ret = fd_close(&test_proc, fd1);
    if (close_ret != 0 || test_proc.fds[fd1] != NULL) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] fd_close failed\n");
        return -1;
    }
    /* Double close should return EBADF */
    if (fd_close(&test_proc, fd1) != SYSCALL_EBADF) {
        vga_puts("[FAIL] Double close did not return EBADF\n");
        return -1;
    }
    /* Read on closed fd should fail */
    if (fd_read(&test_proc, fd1, buf, 5) != SYSCALL_EBADF) {
        vga_puts("[FAIL] Read on closed fd did not return EBADF\n");
        return -1;
    }
    /* Invalid descriptor range checks */
    if (fd_read(&test_proc, -1, buf, 5) != SYSCALL_EBADF ||
        fd_read(&test_proc, MAX_PROCESS_FDS, buf, 5) != SYSCALL_EBADF ||
        fd_write(&test_proc, -1, buf, 5) != SYSCALL_EBADF ||
        fd_write(&test_proc, MAX_PROCESS_FDS, buf, 5) != SYSCALL_EBADF ||
        fd_close(&test_proc, -1) != SYSCALL_EBADF ||
        fd_close(&test_proc, MAX_PROCESS_FDS) != SYSCALL_EBADF) {
        vga_puts("[FAIL] Out of range FD did not return EBADF\n");
        return -1;
    }
    vga_puts("[OK] File close and EBADF validation\n");

    /* 6. Reuse fd: next open should pick fd 3 */
    int fd3 = fd_open(&test_proc, "/readme.txt", O_RDONLY);
    if (fd3 != 3) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] FD reuse failed: expected fd 3\n");
        return -1;
    }
    vga_puts("[OK] Lowest available FD reuse\n");

    /* 7. Write to writable file via FD */
    vfs_node_t *created_node = NULL;
    vfs_create("/test_write_fd.txt", &created_node);
    int fd_wr = fd_open(&test_proc, "/test_write_fd.txt", O_RDWR);
    if (fd_wr < 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Open writable file failed\n");
        return -1;
    }
    const char *test_data = "Stage 11B Data";
    int64_t written = fd_write(&test_proc, fd_wr, test_data, 14);
    if (written != 14 || test_proc.fds[fd_wr]->offset != 14) {
        vga_puts("[FAIL] fd_write failed\n");
        return -1;
    }
    /* Read back using independent open */
    int fd_rd = fd_open(&test_proc, "/test_write_fd.txt", O_RDONLY);
    char rd_buf[32];
    int64_t r_count = fd_read(&test_proc, fd_rd, rd_buf, 14);
    if (r_count != 14 || rd_buf[0] != 'S') {
        vga_puts("[FAIL] Read back written data failed\n");
        return -1;
    }
    vga_puts("[OK] File write and readback\n");

    /* 8. Reject read on directory */
    int fd_dir = fd_open(&test_proc, "/bin", O_RDONLY);
    if (fd_dir < 0) {
        vga_puts("[FAIL] Open directory failed\n");
        return -1;
    }
    if (fd_read(&test_proc, fd_dir, buf, 10) != SYSCALL_EISDIR) {
        vga_puts("[FAIL] Directory read did not return EISDIR\n");
        return -1;
    }
    vga_puts("[OK] Directory read rejection (EISDIR)\n");

    /* 9. Reject write on read-only file */
    if (fd_write(&test_proc, fd3, "bad", 3) != SYSCALL_EACCES) {
        vga_puts("[FAIL] Write on read-only FD should return EACCES\n");
        return -1;
    }
    vga_puts("[OK] Write permission enforcement (EACCES)\n");

    /* 10. Clean up test process descriptors */
    fd_close_all(&test_proc);
    vga_puts("[OK] Clean process FD teardown\n");

    /* 11. Security negative checks */
    char path_dst[64];
    if (syscall_copy_user_path(NULL, path_dst, sizeof(path_dst)) != SYSCALL_EINVAL ||
        syscall_copy_user_path((const char *)0x100000, path_dst, sizeof(path_dst)) != SYSCALL_EFAULT ||
        syscall_validate_writable_user_buffer((void *)0x100000, 16) ||
        syscall_validate_writable_user_buffer((void *)0x50000000, 16) ||
        syscall_validate_writable_user_buffer((void *)USER_CODE_VADDR, 16) ||
        syscall_validate_writable_user_buffer((void *)(USER_STACK_TOP - 8), 16) ||
        syscall_validate_user_buffer((const void *)0x100000, 16) ||
        syscall_validate_user_buffer((const void *)0x50000000, 16) ||
        syscall_validate_user_buffer((const void *)(USER_STACK_TOP - 8), 16) ||
        syscall_validate_user_buffer((const void *)0xFFFFFFFFFFFFFFFEULL, 16) ||
        syscall_validate_user_buffer((const void *)0x7FFFFFF0ULL, 32)) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Pointer validation negative check failed\n");
        return -1;
    }

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("All File Descriptor tests passed successfully!\n");
    return 0;
}
