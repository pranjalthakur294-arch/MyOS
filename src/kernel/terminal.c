#include "terminal.h"
#include "vga.h"
#include "shell.h"
#include "process.h"
#include "scheduler.h"

/* Fixed-size static line editing buffer in BSS */
static char term_edit_buf[TERMINAL_BUFFER_SIZE];
static size_t term_edit_len = 0;

/* Fixed-size static ready buffer in BSS for submitted line input */
static char term_ready_buf[TERMINAL_BUFFER_SIZE];
static size_t term_ready_len = 0;
static size_t term_ready_pos = 0;

/* Currently active foreground reader process */
static process_t *terminal_active_reader = NULL;

/* Forward declarations for VFS node operations */
static int terminal_vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read);
static int terminal_vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written);

/* Static VFS node and operations table for terminal console device */
static vfs_node_ops_t terminal_ops = {
    .read    = terminal_vfs_read,
    .write   = terminal_vfs_write,
    .lookup  = NULL,
    .create  = NULL,
    .mkdir   = NULL,
    .readdir = NULL,
    .unlink  = NULL,
    .release = NULL
};

static vfs_node_t terminal_node = {
    .name          = "console",
    .type          = VFS_NODE_DEVICE,
    .size          = 0,
    .permissions   = 0666,
    .ref_count     = 0,
    .parent        = NULL,
    .ops           = &terminal_ops,
    .fs            = NULL,
    .internal_data = NULL
};

/*
 * terminal_get_vfs_node - Returns the singleton VFS device node representing the terminal.
 */
vfs_node_t *terminal_get_vfs_node(void) {
    return &terminal_node;
}

/*
 * terminal_print_prompt - Renders the standard MyOS prompt string.
 */
void terminal_print_prompt(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("MyOS> ");
    /* Restore text color to white for user typing */
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/*
 * terminal_init - Prepares input buffers, clears active reader, and prints initial prompt.
 */
void terminal_init(void) {
    term_edit_len = 0;
    term_edit_buf[0] = '\0';
    term_ready_len = 0;
    term_ready_pos = 0;
    terminal_active_reader = NULL;
    terminal_node.ref_count = 0;
    terminal_print_prompt();
}

/*
 * terminal_cancel_reader - Discards active reader reference if owned by exiting process.
 */
void terminal_cancel_reader(void *proc_ptr) {
    __asm__ volatile ("cli");
    if (terminal_active_reader == (process_t *)proc_ptr) {
        terminal_active_reader = NULL;
        term_ready_len = 0;
        term_ready_pos = 0;
        term_edit_len = 0;
        term_edit_buf[0] = '\0';
        process_t *p = (process_t *)proc_ptr;
        if (p && p->state == PROCESS_BLOCKED) {
            p->state = PROCESS_READY;
            if (p->task && p->task->state == TASK_BLOCKED) {
                p->task->state = TASK_READY;
            }
        }
    }
    __asm__ volatile ("sti");
}

/*
 * terminal_read - Reads input from the terminal line buffer with blocking and lost-wakeup protection.
 *
 * Parameters:
 *   buf        - Destination buffer in kernel address space.
 *   count      - Maximum number of bytes to read.
 *   bytes_read - Output pointer for actual bytes transferred.
 *
 * Returns:
 *   VFS_OK on success.
 *   VFS_ERR_BUSY (-11) if another process is already reading (single active reader policy).
 *   VFS_ERR_INVALID on NULL pointers.
 */
int terminal_read(void *buf, size_t count, size_t *bytes_read) {
    if (buf == NULL || bytes_read == NULL) {
        return VFS_ERR_INVALID;
    }

    if (count == 0) {
        *bytes_read = 0;
        return VFS_OK;
    }

    process_t *caller = process_current();
    if (!caller) {
        caller = process_get(0);
    }
    if (!caller) {
        return VFS_ERR_IO;
    }

    /* Critical section for reader registration and availability check */
    __asm__ volatile ("cli");

    /* Single active reader policy: deterministic rejection (-EBUSY) if another process is reading */
    if (terminal_active_reader != NULL && terminal_active_reader != caller) {
        __asm__ volatile ("sti");
        return VFS_ERR_BUSY;
    }

    terminal_active_reader = caller;

    /* Wait until input line has been submitted via Enter (\n) */
    while (term_ready_pos >= term_ready_len) {
        caller->state = PROCESS_BLOCKED;
        if (caller->task) {
            caller->task->state = TASK_BLOCKED;
        }

        /* Atomically yield CPU with lost-wakeup protection */
        scheduler_yield();

        /* Resumed after wake-up: re-enter cli to safely inspect buffers */
        __asm__ volatile ("cli");
        if (terminal_active_reader != caller) {
            __asm__ volatile ("sti");
            return VFS_ERR_IO;
        }
        caller->state = PROCESS_RUNNING;
        if (caller->task) {
            caller->task->state = TASK_RUNNING;
        }
    }

    /* Consume available bytes from term_ready_buf */
    size_t available = term_ready_len - term_ready_pos;
    size_t to_copy = (count < available) ? count : available;

    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < to_copy; i++) {
        dst[i] = (uint8_t)term_ready_buf[term_ready_pos + i];
    }

    term_ready_pos += to_copy;

    /* If entire submitted line has been consumed, reset ready buffer and clear active reader */
    if (term_ready_pos >= term_ready_len) {
        term_ready_pos = 0;
        term_ready_len = 0;
        terminal_active_reader = NULL;
    }

    *bytes_read = to_copy;

    __asm__ volatile ("sti");
    return VFS_OK;
}

/*
 * terminal_vfs_read - VFS node read callback routing to terminal_read.
 */
static int terminal_vfs_read(vfs_node_t *node, void *buffer, uint64_t offset, size_t size, size_t *bytes_read) {
    (void)node;
    (void)offset;
    return terminal_read(buffer, size, bytes_read);
}

/*
 * terminal_vfs_write - VFS node write callback rendering characters to VGA text console.
 */
static int terminal_vfs_write(vfs_node_t *node, const void *buffer, uint64_t offset, size_t size, size_t *bytes_written) {
    (void)node;
    (void)offset;
    if (buffer == NULL && size > 0) {
        return VFS_ERR_INVALID;
    }

    if (size == 0) {
        if (bytes_written != NULL) {
            *bytes_written = 0;
        }
        return VFS_OK;
    }

    const char *p = (const char *)buffer;
    for (size_t i = 0; i < size; i++) {
        vga_putc(p[i]);
    }

    if (bytes_written != NULL) {
        *bytes_written = size;
    }
    return VFS_OK;
}

/*
 * terminal_putc - Processes incoming characters through the line discipline.
 *
 * Enforces:
 *   - Enter ('\n'): If active reader is waiting, transfers line to term_ready_buf
 *     and wakes reader. Otherwise, passes line to shell_execute() and prints prompt.
 *   - Backspace ('\b'): Erases user-entered characters from buffer and VGA.
 *   - Printable characters: Appends to edit buffer and echoes to VGA.
 */
void terminal_putc(char c) {
    if (c == '\n') {
        vga_putc('\n');

        /* If a process is waiting to read, transfer line to ready buffer and wake reader */
        if (terminal_active_reader != NULL) {
            if (term_edit_len < TERMINAL_BUFFER_SIZE - 1) {
                term_edit_buf[term_edit_len++] = '\n';
            }
            for (size_t i = 0; i < term_edit_len; i++) {
                term_ready_buf[i] = term_edit_buf[i];
            }
            term_ready_len = term_edit_len;
            term_ready_pos = 0;
            term_edit_len = 0;
            term_edit_buf[0] = '\0';

            /* Wake up blocked reader */
            if (terminal_active_reader->state == PROCESS_BLOCKED) {
                terminal_active_reader->state = PROCESS_READY;
                if (terminal_active_reader->task &&
                    terminal_active_reader->task->state == TASK_BLOCKED) {
                    terminal_active_reader->task->state = TASK_READY;
                }
            }
            return;
        }

        /* Interactive shell command line */
        char cmd_buf[TERMINAL_BUFFER_SIZE];
        for (size_t i = 0; i < term_edit_len; i++) {
            cmd_buf[i] = term_edit_buf[i];
        }
        cmd_buf[term_edit_len] = '\0';
        size_t cmd_len = term_edit_len;

        term_edit_len = 0;
        term_edit_buf[0] = '\0';

        if (cmd_len > 0) {
            shell_execute(cmd_buf);
        }

        terminal_print_prompt();
        return;
    }

    if (c == '\b') {
        if (term_edit_len > 0) {
            term_edit_len--;
            term_edit_buf[term_edit_len] = '\0';
            vga_backspace();
        }
        return;
    }

    /* Standard printable characters, numbers, symbols, spaces */
    if (term_edit_len < TERMINAL_BUFFER_SIZE - 1) {
        term_edit_buf[term_edit_len++] = c;
        term_edit_buf[term_edit_len] = '\0';
        vga_putc(c);
    }
}
