#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "task.h"
#include "scheduler.h"
#include "gdt.h"
#include "user.h"
#include "syscall.h"
#include "process.h"
#include "elf.h"
#include "vfs.h"
#include "file.h"
#include "ata.h"
#include "block.h"
#include "pfs.h"
#include <stddef.h>

/*
 * Freestanding String Helpers
 * Standard libc functions (strcmp, strlen) are not available in bare metal.
 */

static int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static size_t kstrlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

/*
 * Shell Command Table Structure
 */
typedef void (*command_handler_t)(const char *args);

struct shell_command {
    const char *name;
    const char *description;
    command_handler_t handler;
};

/* Forward declarations of built-in command handlers */
static void builtin_help(const char *args);
static void builtin_clear(const char *args);
static void builtin_about(const char *args);
static void builtin_echo(const char *args);
static void builtin_uptime(const char *args);
static void builtin_meminfo(const char *args);
static void builtin_alloc(const char *args);
static void builtin_free(const char *args);
static void builtin_vmmap(const char *args);
static void builtin_vmtest(const char *args);
static void builtin_heapinfo(const char *args);
static void builtin_heaptest(const char *args);
static void builtin_tasks(const char *args);
static void builtin_tasktest(const char *args);
static void builtin_sched(const char *args);
static void builtin_gdtinfo(const char *args);
static void builtin_usertest(const char *args);
static void builtin_syscalltest(const char *args);
static void builtin_ps(const char *args);
static void builtin_proctest(const char *args);
static void builtin_elftest(const char *args);
static void builtin_vfstest(const char *args);
static void builtin_fdtest(const char *args);
static void builtin_run(const char *args);
static void builtin_pwd(const char *args);
static void builtin_cd(const char *args);
static void builtin_ls(const char *args);
static void builtin_cat(const char *args);
static void builtin_touch(const char *args);
static void builtin_mkdir(const char *args);
static void builtin_rm(const char *args);
static void builtin_diskinfo(const char *args);
static void builtin_disktest(const char *args);
static void builtin_blockinfo(const char *args);
static void builtin_blocktest(const char *args);
static void builtin_pfsinfo(const char *args);
static void builtin_pfsformat(const char *args);
static void builtin_pfsmount(const char *args);
static void builtin_pfscat(const char *args);
static void builtin_pfstest(const char *args);
static void builtin_halt(const char *args);

/*
 * Static command table terminated with a sentinel {NULL, NULL, NULL}.
 */
static const struct shell_command commands[] = {
    {"help",        "List commands",         builtin_help},
    {"clear",       "Clear screen",          builtin_clear},
    {"about",       "System information",    builtin_about},
    {"echo",        "Print arguments",       builtin_echo},
    {"uptime",      "System uptime",         builtin_uptime},
    {"meminfo",     "Physical memory info",  builtin_meminfo},
    {"alloc",       "Allocate 4 KiB frame",  builtin_alloc},
    {"free",        "Free test frame",       builtin_free},
    {"vmmap",       "Virtual memory info",   builtin_vmmap},
    {"vmtest",      "Test virtual mapping",  builtin_vmtest},
    {"heapinfo",    "Kernel heap info",      builtin_heapinfo},
    {"heaptest",    "Test kernel heap",      builtin_heaptest},
    {"tasks",       "Kernel tasks list",     builtin_tasks},
    {"tasktest",    "Test task switching",   builtin_tasktest},
    {"sched",       "Scheduler statistics",  builtin_sched},
    {"gdtinfo",     "GDT and TSS info",      builtin_gdtinfo},
    {"usertest",    "Test Ring 3 user mode", builtin_usertest},
    {"syscalltest", "Test system calls",     builtin_syscalltest},
    {"ps",          "Process status list",   builtin_ps},
    {"proctest",    "Test process isolation",builtin_proctest},
    {"elftest",     "Test ELF64 loader",     builtin_elftest},
    {"vfstest",     "Test VFS and RAMFS",    builtin_vfstest},
    {"fdtest",      "Test file descriptors", builtin_fdtest},
    {"run",         "Execute ELF from VFS",  builtin_run},
    {"pwd",         "Print working dir",     builtin_pwd},
    {"cd",          "Change directory",      builtin_cd},
    {"ls",          "List directory",        builtin_ls},
    {"cat",         "Concatenate file",      builtin_cat},
    {"touch",       "Create empty file",     builtin_touch},
    {"mkdir",       "Create directory",      builtin_mkdir},
    {"rm",          "Remove file",           builtin_rm},
    {"diskinfo",    "Show ATA disk info",    builtin_diskinfo},
    {"disktest",    "Test ATA sector I/O",   builtin_disktest},
    {"blockinfo",   "Show block devices",    builtin_blockinfo},
    {"blocktest",   "Test block device I/O", builtin_blocktest},
    {"pfsinfo",     "Show PFS volume info",  builtin_pfsinfo},
    {"pfsformat",   "Format PFS volume",     builtin_pfsformat},
    {"pfsmount",    "Mount PFS volume",      builtin_pfsmount},
    {"pfscat",      "Read file from PFS",    builtin_pfscat},
    {"pfstest",     "Run PFS test suite",    builtin_pfstest},
    {"halt",        "Halt system",           builtin_halt},
    {NULL,          NULL,                    NULL}
};

/*
 * Built-in Command: help
 * Iterates through the command table and prints commands in two columns
 * to fit within the 25-row screen budget without scrolling off.
 */
static void builtin_help(const char *args) {
    (void)args;
    vga_puts("Available commands:\n");
    for (size_t i = 0; commands[i].name != NULL; i += 2) {
        /* Column 1 */
        vga_puts(" ");
        vga_puts(commands[i].name);
        size_t len1 = 1 + kstrlen(commands[i].name);
        vga_puts(" - ");
        len1 += 3;
        vga_puts(commands[i].description);
        len1 += kstrlen(commands[i].description);

        if (commands[i + 1].name == NULL) {
            vga_putc('\n');
            break;
        }

        /* Pad to column 40 */
        while (len1 < 40) {
            vga_putc(' ');
            len1++;
        }
        /* Column 2 */
        vga_puts(commands[i + 1].name);
        vga_puts(" - ");
        vga_puts(commands[i + 1].description);
        vga_putc('\n');
    }
}

/*
 * Built-in Command: clear
 * Clears the 80x25 VGA screen and resets cursor position to (0,0).
 */
static void builtin_clear(const char *args) {
    (void)args;
    vga_clear();
}

/*
 * Built-in Command: about
 * Displays verified architecture information about the MyOS environment.
 */
static void builtin_about(const char *args) {
    (void)args;
    vga_puts("MyOS - Educational x86-64 Operating System\n");
    vga_puts("Architecture: x86-64 (Long Mode, 64-bit)\n");
    vga_puts("Paging: 4-level identity paging\n");
    vga_puts("Interrupts: 8259 PIC + 256-entry IDT\n");
    vga_puts("Timer: PIT Channel 0 @ 100 Hz (IRQ0 / Vector 0x20)\n");
    vga_puts("Memory: 4 KiB Physical Frame Bitmap Allocator\n");
    vga_puts("VMM: 4 KiB Virtual Page Mapping Active\n");
    vga_puts("Heap: 64 KiB Free-List Dynamic Allocator\n");
    vga_puts("Tasks: Cooperative Context Switching Active\n");
    vga_puts("Scheduler: Timer-Driven Round-Robin Active\n");
    vga_puts("User Mode: Ring 3 Foundation Active\n");
    vga_puts("Syscalls: int 0x80 (SYS_WRITE, SYS_GETTIME)\n");
    vga_puts("Processes: Isolated Address Spaces (CR3) Active\n");
    vga_puts("ELF Loader: ELF64 PT_LOAD Validator Active\n");
    vga_puts("VFS/RAMFS: In-Memory Virtual Filesystem Active\n");
    vga_puts("FD Table: Open/Read/Write/Close Active\n");
    vga_puts("Filesystem Exec: run <path> via VFS/FD Active\n");
    vga_puts("FS Commands: ls, cat, touch, mkdir Active\n");
    vga_puts("CWD/Path Ops: pwd, cd, rm Active\n");
    vga_puts("Disk: Primary ATA PIO Driver Active\n");
    vga_puts("Input: PS/2 Keyboard (IRQ1 / Vector 0x21)\n");
    vga_puts("Display: VGA 80x25 text buffer\n");
}

/*
 * Built-in Command: uptime
 * Displays system uptime in seconds and raw elapsed PIT ticks.
 */
static void builtin_uptime(const char *args) {
    (void)args;
    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = ticks / TIMER_FREQUENCY_HZ;
    vga_puts("Uptime: ");
    vga_print_dec(seconds);
    vga_puts(" seconds\nTicks: ");
    vga_print_dec(ticks);
    vga_putc('\n');
}

/*
 * Built-in Command: meminfo
 * Displays physical memory totals, used, free, and frame size.
 */
static void builtin_meminfo(const char *args) {
    (void)args;
    uint64_t total = pmm_get_total_memory();
    uint64_t used = pmm_get_used_memory();
    uint64_t free = pmm_get_free_memory();

    vga_puts("\nPhysical Memory:\n");
    vga_puts("  Total: ");
    vga_print_dec(total / (1024 * 1024));
    vga_puts(" MB (");
    vga_print_dec(total);
    vga_puts(" bytes)\n");

    vga_puts("  Used:  ");
    vga_print_dec(used / (1024 * 1024));
    vga_puts(" MB (");
    vga_print_dec(used);
    vga_puts(" bytes)\n");

    vga_puts("  Free:  ");
    vga_print_dec(free / (1024 * 1024));
    vga_puts(" MB (");
    vga_print_dec(free);
    vga_puts(" bytes)\n");

    vga_puts("  Frame Size: 4096 bytes\n");
}

/*
 * Demonstration allocation tracking for alloc and free commands
 */
#define MAX_SHELL_TEST_ALLOCS 32
static uint64_t shell_test_frames[MAX_SHELL_TEST_ALLOCS];
static int shell_test_count = 0;

/*
 * Built-in Command: alloc
 * Allocates a single 4 KiB physical frame and prints its physical address.
 */
static void builtin_alloc(const char *args) {
    (void)args;
    if (shell_test_count >= MAX_SHELL_TEST_ALLOCS) {
        vga_puts("Maximum test allocations reached.\n");
        return;
    }
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        vga_puts("No free physical frames available.\n");
        return;
    }
    shell_test_frames[shell_test_count++] = frame;
    vga_puts("Allocated frame: ");
    vga_print_hex(frame);
    vga_putc('\n');
}

/*
 * Built-in Command: free
 * Frees the last allocated test frame and updates accounting.
 */
static void builtin_free(const char *args) {
    (void)args;
    if (shell_test_count == 0) {
        vga_puts("No test frame is currently allocated.\n");
        return;
    }
    uint64_t frame = shell_test_frames[--shell_test_count];
    pmm_free_frame(frame);
    vga_puts("Freed frame: ");
    vga_print_hex(frame);
    vga_putc('\n');
}

/*
 * Built-in Command: vmmap
 * Displays current virtual memory architecture, active PML4 root, and VMM test range.
 */
static void builtin_vmmap(const char *args) {
    (void)args;
    uint64_t cr3 = vmm_read_cr3();
    uint64_t test_phys = 0;
    int mapped = vmm_get_mapping(VMM_TEST_VIRTUAL_ADDRESS, &test_phys);

    vga_puts("\nVirtual Memory:\n");
    vga_puts("  Paging: 4-level\n");
    vga_puts("  Page Size: 4096 bytes\n");
    vga_puts("  Root PML4: ");
    vga_print_hex(cr3 & PTE_ADDR_MASK);
    vga_puts("\n  Identity Map: 0-1 GiB\n");
    vga_puts("  VMM: Active\n");
    vga_puts("  Test Region: ");
    vga_print_hex(VMM_MANAGED_START);
    vga_puts("\n  Test Mapping: ");
    if (mapped == 0) {
        vga_puts("Mapped -> ");
        vga_print_hex(test_phys);
    } else {
        vga_puts("Unmapped");
    }
    vga_putc('\n');
}

/*
 * Built-in Command: vmtest
 * Performs an end-to-end 4 KiB virtual page mapping test:
 *   1. Allocates physical frame via PMM
 *   2. Maps to VMM_TEST_VIRTUAL_ADDRESS (0x40000000)
 *   3. Verifies mapping via vmm_get_mapping()
 *   4. Writes and reads back test pattern through virtual address
 *   5. Cross-verifies through physical identity address
 *   6. Unmaps virtual page and flushes TLB
 *   7. Releases physical frame back to PMM
 */
static void builtin_vmtest(const char *args) {
    (void)args;
    vga_puts("\nVMM Test:\n");

    /* 1. Allocate physical frame */
    uint64_t phys_frame = pmm_alloc_frame();
    if (phys_frame == 0) {
        vga_puts("  [FAIL] Physical frame allocation failed.\n");
        return;
    }
    vga_puts("  Physical frame: ");
    vga_print_hex(phys_frame);
    vga_puts("\n  Virtual address: ");
    vga_print_hex(VMM_TEST_VIRTUAL_ADDRESS);
    vga_putc('\n');

    /* 2. Map virtual address */
    int map_res = vmm_map_page(VMM_TEST_VIRTUAL_ADDRESS, phys_frame, PTE_PRESENT | PTE_WRITABLE);
    if (map_res != 0) {
        vga_puts("  [FAIL] Mapping failed with error ");
        vga_print_dec((uint64_t)(-map_res));
        vga_putc('\n');
        pmm_free_frame(phys_frame);
        return;
    }

    /* 3. Verify mapping query */
    uint64_t resolved = 0;
    if (vmm_get_mapping(VMM_TEST_VIRTUAL_ADDRESS, &resolved) != 0 || resolved != phys_frame) {
        vga_puts("  [FAIL] Mapping verification failed.\n");
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return;
    }
    vga_puts("  Mapping: OK\n");

    /* 4. Write & Read Test Pattern */
    const uint64_t TEST_VAL = 0xCAFEBABE12345678ULL;
    volatile uint64_t *vptr = (volatile uint64_t *)VMM_TEST_VIRTUAL_ADDRESS;
    *vptr = TEST_VAL;

    if (*vptr != TEST_VAL) {
        vga_puts("  [FAIL] Memory readback mismatch.\n");
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return;
    }

    /* Cross-verify via identity map */
    volatile uint64_t *iptr = (volatile uint64_t *)phys_frame;
    if (*iptr != TEST_VAL) {
        vga_puts("  [FAIL] Physical identity mismatch.\n");
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return;
    }
    vga_puts("  Memory access: OK\n");

    /* 5. Unmap page */
    if (vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS) != 0) {
        vga_puts("  [FAIL] Unmapping failed.\n");
        pmm_free_frame(phys_frame);
        return;
    }
    vga_puts("  Unmap: OK\n");

    /* 6. Release physical frame */
    pmm_free_frame(phys_frame);
    vga_puts("  Frame released: OK\n");

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("VMM test passed!\n");
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/*
 * Built-in Command: heapinfo
 * Displays kernel heap location, capacity, allocated and free block statistics.
 */
static void builtin_heapinfo(const char *args) {
    (void)args;
    heap_stats_t stats;
    heap_get_stats(&stats);

    const char *bytes_nl = " bytes\n";
    vga_puts("\nKernel Heap:\n  Start:        ");
    vga_print_hex(stats.start_addr);
    vga_puts("\n  Size:         ");
    vga_print_dec((uint32_t)stats.total_size);
    vga_puts(bytes_nl);
    vga_puts("  Used:         ");
    vga_print_dec((uint32_t)stats.used_bytes);
    vga_puts(bytes_nl);
    vga_puts("  Free:         ");
    vga_print_dec((uint32_t)stats.free_bytes);
    vga_puts(bytes_nl);
    vga_puts("  Blocks:       ");
    vga_print_dec((uint32_t)stats.total_blocks);
    vga_puts("\n  Free Blocks:  ");
    vga_print_dec((uint32_t)stats.free_blocks);
    vga_puts("\n  Used Blocks:  ");
    vga_print_dec((uint32_t)stats.used_blocks);
    vga_puts("\n  Largest Free: ");
    vga_print_dec((uint32_t)stats.largest_free);
    vga_puts(bytes_nl);
}

/*
 * Built-in Command: heaptest
 * Executes the comprehensive in-kernel heap verification test suite.
 */
static void builtin_heaptest(const char *args) {
    (void)args;
    vga_putc('\n');
    heap_run_test();
}

/*
 * Built-in Command: tasks
 * Displays the current kernel task list and execution states.
 */
static void builtin_tasks(const char *args) {
    (void)args;
    task_print_list();
}

/*
 * Built-in Command: tasktest
 * Executes the manual cooperative context switching verification routine.
 */
static void builtin_tasktest(const char *args) {
    (void)args;
    vga_putc('\n');
    task_run_demo();
}

/*
 * Built-in Command: sched
 * Displays round-robin scheduler statistics and active tasks.
 */
static void builtin_sched(const char *args) {
    (void)args;
    scheduler_print_stats();
}

/*
 * Built-in Command: echo
 * Prints arguments directly to screen, followed by a newline.
 * If no arguments are provided, prints a blank line.
 */
static void builtin_echo(const char *args) {
    if (args != NULL && *args != '\0') {
        vga_puts(args);
    }
    vga_putc('\n');
}

/*
 * Built-in Command: gdtinfo
 * Displays GDT descriptor and TSS configuration.
 */
static void builtin_gdtinfo(const char *args) {
    (void)args;
    gdt_print_info();
}

/*
 * Built-in Command: usertest
 * Executes Ring 3 user mode transition and prints verification results.
 */
static void builtin_usertest(const char *args) {
    (void)args;
    user_print_status();
}

/*
 * Built-in Command: syscalltest
 * Executes Ring 3 system call verification suite and prints report.
 */
static void builtin_syscalltest(const char *args) {
    (void)args;
    syscall_print_status();
}

/*
 * Built-in Command: ps
 * Displays active and terminated processes in the system.
 */
static void builtin_ps(const char *args) {
    (void)args;
    process_print_list();
}

/*
 * Built-in Command: proctest
 * Executes Stage 9 multi-process isolation and scheduling verification suite.
 */
static void builtin_proctest(const char *args) {
    (void)args;
    process_print_test_status();
}

/*
 * Built-in Command: elftest
 * Executes Stage 10 ELF64 loader validation and execution test suite.
 */
static void builtin_elftest(const char *args) {
    (void)args;
    elf_print_test_status();
}

/*
 * Built-in Command: vfstest
 * Executes Stage 11A VFS and RAMFS verification test suite.
 */
static void builtin_vfstest(const char *args) {
    (void)args;
    vga_puts("Running Stage 11A VFS and RAMFS verification suite...\n");
    int res = vfs_run_tests();
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("All VFS tests passed successfully!\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("VFS test failure detected!\n");
    }
}

/*
 * Built-in Command: fdtest
 * Executes Stage 11B file descriptor and open/read/write/close verification suite.
 */
static void builtin_fdtest(const char *args) {
    (void)args;
    fd_run_tests();
}

/*
 * Built-in Command: run
 * Loads and executes an ELF64 executable from a VFS path in Ring 3.
 * Usage: run <path>
 */
static void builtin_run(const char *args) {
    if (args == NULL || *args == '\0') {
        vga_puts("Usage: run <path>\n");
        return;
    }

    /* Skip leading whitespace */
    const char *p = args;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        vga_puts("Usage: run <path>\n");
        return;
    }

    /* Extract single whitespace-delimited path token */
    char path[VFS_PATH_MAX];
    size_t len = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && len + 1 < sizeof(path)) {
        path[len++] = *p++;
    }
    path[len] = '\0';

    /* Execute ELF through VFS/FD pipeline */
    process_t *proc = NULL;
    int err = process_exec_path(path, "user_proc", &proc);
    if (err != 0 || !proc) {
        if (err == ELF_ERR_NOT_FOUND) {
            vga_puts("Error: File not found: ");
            vga_puts(path);
            vga_putc('\n');
        } else if (err == ELF_ERR_IS_DIR) {
            vga_puts("Error: Cannot execute directory: ");
            vga_puts(path);
            vga_putc('\n');
        } else {
            vga_puts("Error: Failed to load ELF '");
            vga_puts(path);
            vga_puts("': ");
            vga_puts(elf_strerror(err));
            vga_putc('\n');
        }
        return;
    }

    /* Allow process to execute in Ring 3 under timer-driven scheduler */
    __asm__ volatile ("sti");
    uint64_t start_tick = timer_get_ticks();
    while ((timer_get_ticks() - start_tick) < 200) {
        if (proc->state == PROCESS_TERMINATED || proc->reaped) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    /* Cleanly reap terminated process and return physical frames to PMM */
    process_reap_terminated();
}

/*
 * parse_single_path_arg - Extracts a single path argument token.
 * Returns:
 *    0 : single path argument successfully extracted
 *   -1 : no path argument supplied (empty / whitespace only)
 *   -2 : excess argument tokens supplied
 */
static int parse_single_path_arg(const char *args, char *out_path, size_t max_len) {
    if (args == NULL || *args == '\0') {
        return -1;
    }
    const char *p = args;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return -1;
    }

    size_t len = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (len + 1 < max_len) {
            out_path[len++] = *p;
        }
        p++;
    }
    out_path[len] = '\0';

    /* Check for trailing excess argument tokens */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '\0') {
        return -2;
    }

    return 0;
}

/*
 * Built-in Command: pwd
 * Prints the current working directory of the current process.
 * Usage: pwd
 */
static void builtin_pwd(const char *args) {
    (void)args;
    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
        if (!proc) {
            vga_puts("/\n");
            return;
        }
    }

    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();
    char buf[VFS_PATH_MAX];
    int err = vfs_get_path(cwd, buf, sizeof(buf));
    if (err == VFS_OK) {
        vga_puts(buf);
        vga_putc('\n');
    } else {
        vga_puts("/\n");
    }
}

/*
 * Built-in Command: cd
 * Changes the current working directory of the current process.
 * Usage: cd <path>
 */
static void builtin_cd(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res != 0) {
        vga_puts("Usage: cd <path>\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
        if (!proc) {
            vga_puts("cd: error obtaining process context\n");
            return;
        }
    }

    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();
    vfs_node_t *target_node = NULL;
    int err = vfs_lookup_from(cwd, path, &target_node);
    if (err != VFS_OK || target_node == NULL) {
        vga_puts("cd: ");
        vga_puts(path);
        vga_puts(": No such file or directory\n");
        return;
    }

    if (target_node->type != VFS_NODE_DIRECTORY) {
        vga_puts("cd: ");
        vga_puts(path);
        vga_puts(": Not a directory\n");
        return;
    }

    process_set_cwd(proc, target_node);
}

/*
 * Built-in Command: ls
 * Lists contents of a directory.
 * Usage: ls [path] (defaults to current working directory if omitted)
 */
static void builtin_ls(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res == -2) {
        vga_puts("Usage: ls [path]\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
    }
    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();

    vfs_node_t *dir_node = NULL;
    int err = VFS_OK;
    if (res == -1) {
        dir_node = cwd;
    } else {
        err = vfs_lookup_from(cwd, path, &dir_node);
    }

    if (err != VFS_OK || dir_node == NULL) {
        vga_puts("ls: cannot access '");
        vga_puts(path);
        vga_puts("': No such file or directory\n");
        return;
    }

    if (dir_node->type != VFS_NODE_DIRECTORY) {
        vga_puts("ls: cannot access '");
        vga_puts(path);
        vga_puts(": Not a directory\n");
        return;
    }

    uint64_t idx = 0;
    vfs_dirent_t dirent;
    while (1) {
        int r = vfs_readdir(dir_node, idx, &dirent);
        if (r == VFS_EOF) {
            break;
        }
        if (r != VFS_OK) {
            vga_puts("ls: error reading directory '");
            if (res == -1) {
                char pbuf[VFS_PATH_MAX];
                if (vfs_get_path(cwd, pbuf, sizeof(pbuf)) == VFS_OK) {
                    vga_puts(pbuf);
                } else {
                    vga_puts("/");
                }
            } else {
                vga_puts(path);
            }
            vga_puts("'\n");
            break;
        }

        vga_puts(dirent.name);
        if (dirent.type == VFS_NODE_DIRECTORY) {
            vga_putc('/');
        }
        vga_putc('\n');
        idx++;
    }
}

/*
 * Built-in Command: cat
 * Displays contents of a file through the File Descriptor layer.
 * Usage: cat <path>
 */
static void builtin_cat(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res != 0) {
        vga_puts("Usage: cat <path>\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
        if (!proc) {
            vga_puts("cat: error obtaining process context\n");
            return;
        }
    }

    int fd = fd_open(proc, path, O_RDONLY);
    if (fd < 0) {
        if (fd == SYSCALL_ENOENT) {
            vga_puts("cat: ");
            vga_puts(path);
            vga_puts(": No such file or directory\n");
        } else if (fd == SYSCALL_EISDIR) {
            vga_puts("cat: ");
            vga_puts(path);
            vga_puts(": Is a directory\n");
        } else {
            vga_puts("cat: ");
            vga_puts(path);
            vga_puts(": Cannot open file\n");
        }
        return;
    }

    /* Bounded buffer chunk read */
    char buf[128];
    while (1) {
        int64_t n = fd_read(proc, fd, buf, sizeof(buf));
        if (n == 0) {
            break; /* EOF */
        }
        if (n < 0) {
            if (n == SYSCALL_EISDIR) {
                vga_puts("cat: ");
                vga_puts(path);
                vga_puts(": Is a directory\n");
            } else {
                vga_puts("cat: read error\n");
            }
            break;
        }

        for (int64_t i = 0; i < n; i++) {
            vga_putc(buf[i]);
        }
    }

    fd_close(proc, fd);
}

/*
 * Built-in Command: touch
 * Creates a new empty regular file through VFS.
 * Usage: touch <path>
 */
static void builtin_touch(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res != 0) {
        vga_puts("Usage: touch <path>\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
    }
    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();

    vfs_node_t *node = NULL;
    int err = vfs_create_from(cwd, path, &node);
    if (err != VFS_OK) {
        if (err == VFS_ERR_EXISTS) {
            vga_puts("touch: cannot touch '");
            vga_puts(path);
            vga_puts("': File already exists\n");
        } else if (err == VFS_ERR_NOT_FOUND) {
            vga_puts("touch: cannot touch '");
            vga_puts(path);
            vga_puts("': No such file or directory\n");
        } else if (err == VFS_ERR_NOT_DIR) {
            vga_puts("touch: cannot touch '");
            vga_puts(path);
            vga_puts("': Not a directory\n");
        } else if (err == VFS_ERR_NO_MEM) {
            vga_puts("touch: cannot touch '");
            vga_puts(path);
            vga_puts("': Out of memory\n");
        } else {
            vga_puts("touch: cannot touch '");
            vga_puts(path);
            vga_puts("': Invalid path\n");
        }
    }
}

/*
 * Built-in Command: mkdir
 * Creates a new directory through VFS.
 * Usage: mkdir <path>
 */
static void builtin_mkdir(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res != 0) {
        vga_puts("Usage: mkdir <path>\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
    }
    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();

    vfs_node_t *node = NULL;
    int err = vfs_mkdir_from(cwd, path, &node);
    if (err != VFS_OK) {
        if (err == VFS_ERR_EXISTS) {
            vga_puts("mkdir: cannot create directory '");
            vga_puts(path);
            vga_puts("': File exists\n");
        } else if (err == VFS_ERR_NOT_FOUND) {
            vga_puts("mkdir: cannot create directory '");
            vga_puts(path);
            vga_puts("': No such file or directory\n");
        } else if (err == VFS_ERR_NOT_DIR) {
            vga_puts("mkdir: cannot create directory '");
            vga_puts(path);
            vga_puts("': Not a directory\n");
        } else if (err == VFS_ERR_NO_MEM) {
            vga_puts("mkdir: cannot create directory '");
            vga_puts(path);
            vga_puts("': Out of memory\n");
        } else {
            vga_puts("mkdir: cannot create directory '");
            vga_puts(path);
            vga_puts("': Invalid path\n");
        }
    }
}

/*
 * Built-in Command: rm
 * Removes a regular file from the filesystem.
 * Usage: rm <path>
 */
static void builtin_rm(const char *args) {
    char path[VFS_PATH_MAX];
    int res = parse_single_path_arg(args, path, sizeof(path));
    if (res != 0) {
        vga_puts("Usage: rm <path>\n");
        return;
    }

    process_t *proc = process_current();
    if (!proc) {
        proc = process_get(0);
        if (!proc) {
            vga_puts("rm: error obtaining process context\n");
            return;
        }
    }

    vfs_node_t *cwd = (proc && proc->cwd) ? proc->cwd : vfs_get_root();
    int err = vfs_unlink_from(cwd, path);
    if (err != VFS_OK) {
        if (err == VFS_ERR_NOT_FOUND) {
            vga_puts("rm: cannot remove '");
            vga_puts(path);
            vga_puts("': No such file or directory\n");
        } else if (err == VFS_ERR_IS_DIR) {
            vga_puts("rm: cannot remove '");
            vga_puts(path);
            vga_puts("': Is a directory\n");
        } else {
            vga_puts("rm: cannot remove '");
            vga_puts(path);
            vga_puts("': Invalid path\n");
        }
    }
}

/*
 * Built-in Command: diskinfo
 * Displays concise ATA Primary Master drive parameters.
 */
static void builtin_diskinfo(const char *args) {
    (void)args;
    vga_puts("ATA Disk Information:\n");
    vga_puts("Channel: Primary\n");
    vga_puts("Device: Master\n");
    if (!ata_is_present()) {
        vga_puts("Present: No\n");
        return;
    }
    vga_puts("Present: Yes\n");
    vga_puts("Model: ");
    vga_puts(ata_get_model());
    vga_putc('\n');
    vga_puts("Sector Size: ");
    vga_print_dec(ata_get_sector_size());
    vga_puts(" bytes\n");
    vga_puts("LBA28 Sectors: ");
    uint32_t count = ata_get_sector_count();
    vga_print_dec(count);
    vga_putc('\n');
    vga_puts("Capacity: ");
    vga_print_dec(count / 2048);
    vga_puts(" MB\n");
}

static uint8_t s_orig_sector[ATA_SECTOR_SIZE];
static uint8_t s_test_sector[ATA_SECTOR_SIZE];
static uint8_t s_verify_sector[ATA_SECTOR_SIZE];

/*
 * Built-in Command: disktest
 * Executes a non-destructive verification sequence on reserved sector LBA 8:
 * 1. Read original sector
 * 2. Write deterministic test pattern
 * 3. Verify all 512 bytes read back
 * 4. Restore original sector and verify restoration
 */
static void builtin_disktest(const char *args) {
    (void)args;
    if (!ata_is_present()) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("ATA disk not present\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    /* Negative parameter validation */
    if (ata_read_sector(0, NULL) != ATA_ERR_INVALID ||
        ata_read_sector(0xFFFFFFFF, s_orig_sector) != ATA_ERR_INVALID ||
        ata_read_sector(ata_get_sector_count(), s_orig_sector) != ATA_ERR_INVALID ||
        ata_write_sector(0, NULL) != ATA_ERR_INVALID ||
        ata_write_sector(0xFFFFFFFF, s_orig_sector) != ATA_ERR_INVALID ||
        ata_write_sector(ata_get_sector_count(), s_orig_sector) != ATA_ERR_INVALID) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("Negative validation failed\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    vga_puts("ATA PIO Disk Test (LBA 8):\n");

    /* Step 1: Read original sector */
    vga_puts("[1/4] Reading original sector... ");
    int rc = ata_read_sector(ATA_TEST_LBA, s_orig_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    vga_puts("OK\n");

    /* Step 2: Consecutive writes with distinct patterns (verifies write safety & cache flush) */
    vga_puts("[2/4] Writing test pattern... ");
    /* Write pattern 1: (i ^ 0x5A) */
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        s_test_sector[i] = (uint8_t)(i ^ 0x5A);
    }
    rc = ata_write_sector(ATA_TEST_LBA, s_test_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED p1 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        ata_write_sector(ATA_TEST_LBA, s_orig_sector);
        return;
    }

    /* Write pattern 2: (i ^ 0x3C) */
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        s_test_sector[i] = (uint8_t)(i ^ 0x3C);
    }
    rc = ata_write_sector(ATA_TEST_LBA, s_test_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED p2 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        ata_write_sector(ATA_TEST_LBA, s_orig_sector);
        return;
    }

    /* Write pattern 3: (i ^ 0xA5) */
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        s_test_sector[i] = (uint8_t)(i ^ 0xA5);
    }
    rc = ata_write_sector(ATA_TEST_LBA, s_test_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED p3 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        ata_write_sector(ATA_TEST_LBA, s_orig_sector);
        return;
    }
    vga_puts("OK\n");

    /* Step 3: Read back and verify pattern */
    vga_puts("[3/4] Verifying test pattern... ");
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        s_verify_sector[i] = 0;
    }
    rc = ata_read_sector(ATA_TEST_LBA, s_verify_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED read (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        ata_write_sector(ATA_TEST_LBA, s_orig_sector);
        return;
    }

    bool matched = true;
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (s_verify_sector[i] != (uint8_t)(i ^ 0xA5)) {
            matched = false;
            break;
        }
    }
    if (!matched) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED data mismatch\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        ata_write_sector(ATA_TEST_LBA, s_orig_sector);
        return;
    }
    vga_puts("OK\n");

    /* Step 4: Restore original sector and verify restoration */
    vga_puts("[4/4] Restoring original sector... ");
    rc = ata_write_sector(ATA_TEST_LBA, s_orig_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED write (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    rc = ata_read_sector(ATA_TEST_LBA, s_verify_sector);
    if (rc != ATA_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED reread (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    bool restored = true;
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (s_verify_sector[i] != s_orig_sector[i]) {
            restored = false;
            break;
        }
    }
    if (!restored) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED restore mismatch\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    vga_puts("OK\n");

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Disk test passed!\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/*
 * Built-in Command: blockinfo
 * Displays registered generic block devices and their parameters.
 */
static void builtin_blockinfo(const char *args) {
    (void)args;
    vga_puts("Block Devices\n");
    vga_puts("-------------\n");
    size_t count = block_device_count();
    if (count == 0) {
        vga_puts("No block devices registered.\n");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        block_device_t *dev = block_get_by_index(i);
        if (!dev) {
            continue;
        }
        vga_puts("Name: ");
        vga_puts(block_name(dev));
        vga_putc('\n');
        vga_puts("Type: ATA\n");
        vga_puts("Sector Size: ");
        vga_print_dec(block_sector_size(dev));
        vga_putc('\n');
        vga_puts("Sectors: ");
        vga_print_dec(block_sector_count(dev));
        vga_putc('\n');
        vga_puts("Capacity: ");
        uint64_t cap_mb = ((uint64_t)block_sector_count(dev) * (uint64_t)block_sector_size(dev)) / (1024ULL * 1024ULL);
        vga_print_dec(cap_mb);
        vga_puts(" MiB\n");
    }
}

static uint8_t s_blk_orig[512];
static uint8_t s_blk_test[512];
static uint8_t s_blk_verify[512];

/*
 * Built-in Command: blocktest
 * Executes generic API validation and a non-destructive verification sequence
 * on reserved sector 8 through the generic block layer (block_read / block_write).
 */
static void builtin_blocktest(const char *args) {
    (void)args;
    block_device_t *dev = block_get("ata0");
    if (!dev) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("No block device available\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    /* Generic Block API Negative Validation */
    block_device_t dummy = {0};
    if (block_read(NULL, 0, s_blk_orig) != BLOCK_ERR_INVALID ||
        block_read(dev, 0, NULL) != BLOCK_ERR_INVALID ||
        block_read(&dummy, 0, s_blk_orig) != BLOCK_ERR_INVALID ||
        block_read(dev, block_sector_count(dev), s_blk_orig) != BLOCK_ERR_RANGE ||
        block_read(dev, 0xFFFFFFFF, s_blk_orig) != BLOCK_ERR_RANGE ||
        block_write(NULL, 0, s_blk_orig) != BLOCK_ERR_INVALID ||
        block_write(dev, 0, NULL) != BLOCK_ERR_INVALID ||
        block_write(&dummy, 0, s_blk_orig) != BLOCK_ERR_INVALID ||
        block_write(dev, block_sector_count(dev), s_blk_orig) != BLOCK_ERR_RANGE ||
        block_write(dev, 0xFFFFFFFF, s_blk_orig) != BLOCK_ERR_RANGE ||
        block_get(NULL) != NULL ||
        block_get("nonexistent_device") != NULL ||
        block_register(NULL) != BLOCK_ERR_INVALID ||
        block_register(dev) != BLOCK_ERR_EXIST) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("Generic API validation failed\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    const uint32_t test_sec = 8;
    if (test_sec >= block_sector_count(dev)) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("Test sector out of range\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    vga_puts("Generic Block Device Test (ata0, Sector 8):\n");

    /* Step 1: Read original sector */
    vga_puts("[1/4] Reading original sector... ");
    int rc = block_read(dev, test_sec, s_blk_orig);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED read (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    vga_puts("OK\n");

    /* Step 2: Write test pattern 1 and verify */
    vga_puts("[2/4] Writing test pattern 1... ");
    for (int i = 0; i < 512; i++) {
        s_blk_test[i] = (uint8_t)(i ^ 0x33);
        s_blk_verify[i] = 0;
    }
    rc = block_write(dev, test_sec, s_blk_test);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED write1 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        block_write(dev, test_sec, s_blk_orig);
        return;
    }
    rc = block_read(dev, test_sec, s_blk_verify);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED reread1 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        block_write(dev, test_sec, s_blk_orig);
        return;
    }
    for (int i = 0; i < 512; i++) {
        if (s_blk_verify[i] != s_blk_test[i]) {
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("FAILED mismatch1\n");
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            block_write(dev, test_sec, s_blk_orig);
            return;
        }
    }
    vga_puts("OK\n");

    /* Step 3: Write test pattern 2 and verify */
    vga_puts("[3/4] Writing test pattern 2... ");
    for (int i = 0; i < 512; i++) {
        s_blk_test[i] = (uint8_t)(i ^ 0xCC);
        s_blk_verify[i] = 0;
    }
    rc = block_write(dev, test_sec, s_blk_test);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED write2 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        block_write(dev, test_sec, s_blk_orig);
        return;
    }
    rc = block_read(dev, test_sec, s_blk_verify);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED reread2 (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        block_write(dev, test_sec, s_blk_orig);
        return;
    }
    for (int i = 0; i < 512; i++) {
        if (s_blk_verify[i] != s_blk_test[i]) {
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("FAILED mismatch2\n");
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            block_write(dev, test_sec, s_blk_orig);
            return;
        }
    }
    vga_puts("OK\n");

    /* Step 4: Restore original sector and verify */
    vga_puts("[4/4] Restoring original sector... ");
    rc = block_write(dev, test_sec, s_blk_orig);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED restore write (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    rc = block_read(dev, test_sec, s_blk_verify);
    if (rc != BLOCK_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED restore read (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    for (int i = 0; i < 512; i++) {
        if (s_blk_verify[i] != s_blk_orig[i]) {
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("FAILED restore mismatch\n");
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            return;
        }
    }
    vga_puts("OK\n");

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Block test passed!\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/*
 * Built-in Command: pfsinfo
 * Displays persistent filesystem status and on-disk geometry.
 */
static void builtin_pfsinfo(const char *args) {
    (void)args;
    if (!pfs_is_mounted()) {
        block_device_t *dev = block_get("ata0");
        if (dev != NULL) {
            pfs_mount(dev);
        }
    }

    if (!pfs_is_mounted()) {
        vga_puts("PFS: Not mounted (no valid filesystem found)\n");
        return;
    }

    const pfs_superblock_t *sb = pfs_get_superblock();
    block_device_t *dev = pfs_get_device();
    vga_puts("Persistent Filesystem (PFS) Info:\n");
    vga_puts("Device: ");
    vga_puts(dev ? block_name(dev) : "unknown");
    vga_putc('\n');
    vga_puts("Magic: ");
    vga_print_hex(sb->magic);
    vga_puts(" | Version: ");
    vga_print_dec(sb->version);
    vga_putc('\n');
    vga_puts("Sector Size: ");
    vga_print_dec(sb->sector_size);
    vga_puts(" | Total Sectors: ");
    vga_print_dec(sb->total_sectors);
    vga_putc('\n');
    vga_puts("Block Bitmap: Sectors ");
    vga_print_dec(sb->block_bitmap_start);
    vga_puts("..");
    vga_print_dec(sb->block_bitmap_start + sb->block_bitmap_sectors - 1);
    vga_puts(" (");
    vga_print_dec(sb->block_bitmap_sectors);
    vga_puts(" secs)\n");
    vga_puts("Inode Bitmap: Sector ");
    vga_print_dec(sb->inode_bitmap_start);
    vga_puts(" | Inode Table: Sectors ");
    vga_print_dec(sb->inode_table_start);
    vga_puts("..");
    vga_print_dec(sb->inode_table_start + sb->inode_table_sectors - 1);
    vga_putc('\n');
    vga_puts("Data Region: Sector ");
    vga_print_dec(sb->data_start);
    vga_puts(" (");
    vga_print_dec(sb->data_blocks);
    vga_puts(" blocks)\n");
    vga_puts("Free Blocks: ");
    vga_print_dec(sb->free_blocks);
    vga_puts(" / ");
    vga_print_dec(sb->data_blocks);
    vga_puts(" | Inodes: ");
    vga_print_dec(sb->inode_count - sb->free_inodes);
    vga_puts(" / ");
    vga_print_dec(sb->inode_count);
    vga_putc('\n');
}

/*
 * Built-in Command: pfsformat
 * Formats a block device with the persistent filesystem.
 * Usage: pfsformat [device] (defaults to "ata0")
 */
static void builtin_pfsformat(const char *args) {
    char dev_name[BLOCK_NAME_MAX];
    int res = parse_single_path_arg(args, dev_name, sizeof(dev_name));
    const char *target = (res == 0) ? dev_name : "ata0";

    block_device_t *dev = block_get(target);
    if (!dev) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("pfsformat: Device not found: ");
        vga_puts(target);
        vga_putc('\n');
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    vga_puts("Formatting ");
    vga_puts(target);
    vga_puts(" with PFS... ");
    int rc = pfs_format(dev);
    if (rc == PFS_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (rc=");
        vga_print_dec((uint64_t)(-rc));
        vga_puts(")\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

/*
 * Built-in Command: pfsmount
 * Mounts an existing PFS volume from a block device.
 * Usage: pfsmount [device] (defaults to "ata0")
 */
static void builtin_pfsmount(const char *args) {
    char dev_name[BLOCK_NAME_MAX];
    int res = parse_single_path_arg(args, dev_name, sizeof(dev_name));
    const char *target = (res == 0) ? dev_name : "ata0";

    block_device_t *dev = block_get(target);
    if (!dev) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("pfsmount: Device not found: ");
        vga_puts(target);
        vga_putc('\n');
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    int rc = pfs_mount(dev);
    if (rc == PFS_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PFS mounted successfully on ");
        vga_puts(target);
        vga_putc('\n');
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("pfsmount: Mount failed (invalid or unformatted filesystem)\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

/*
 * Built-in Command: pfscat
 * Reads and prints contents of a file from the mounted PFS root directory.
 * Usage: pfscat <filename>
 */
static void builtin_pfscat(const char *args) {
    char filename[PFS_NAME_MAX + 1];
    int res = parse_single_path_arg(args, filename, sizeof(filename));
    if (res != 0) {
        vga_puts("Usage: pfscat <filename>\n");
        return;
    }

    if (!pfs_is_mounted()) {
        block_device_t *dev = block_get("ata0");
        if (dev != NULL) {
            pfs_mount(dev);
        }
    }

    if (!pfs_is_mounted()) {
        vga_puts("pfscat: Filesystem not mounted\n");
        return;
    }

    uint32_t ino = 0;
    uint8_t type = 0;
    int rc = pfs_lookup(PFS_ROOT_INODE, filename, &ino, &type);
    if (rc != PFS_OK) {
        vga_puts("pfscat: File not found: ");
        vga_puts(filename);
        vga_putc('\n');
        return;
    }

    if (type != PFS_ENTRY_FILE) {
        vga_puts("pfscat: Not a regular file: ");
        vga_puts(filename);
        vga_putc('\n');
        return;
    }

    static char cat_buf[512];
    uint32_t offset = 0;
    uint32_t bytes_read = 0;
    while (pfs_read_file(ino, offset, cat_buf, sizeof(cat_buf) - 1, &bytes_read) == PFS_OK && bytes_read > 0) {
        cat_buf[bytes_read] = '\0';
        vga_puts(cat_buf);
        offset += bytes_read;
    }
    vga_putc('\n');
}

/*
 * Built-in Command: pfstest
 * Executes the in-kernel PFS verification suite.
 */
static void builtin_pfstest(const char *args) {
    (void)args;
    vga_puts("Running PFS in-kernel verification suite...\n");
    int rc = pfs_run_tests();
    if (rc == PFS_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PFS verification suite passed!\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("PFS verification suite FAILED\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

/*
 * Built-in Command: halt
 * Informs the user, disables hardware interrupts (cli), and halts the CPU.
 * Never returns.
 */
static void builtin_halt(const char *args) {
    (void)args;
    vga_puts("System halted.\n");
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/*
 * shell_init - Prepares shell state.
 */
void shell_init(void) {
    /* Shell is currently stateless; placeholder for future subsystem setup */
}

/*
 * shell_execute - Parses and executes the command string.
 */
void shell_execute(const char *cmd_line) {
    if (cmd_line == NULL) {
        return;
    }

    const char *p = cmd_line;

    /* 1. Skip leading whitespace (spaces and tabs) */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    /* 2. Empty or whitespace-only input */
    if (*p == '\0') {
        return;
    }

    /* 3. Extract the first whitespace-delimited token */
    const char *token_start = p;
    size_t token_len = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        token_len++;
        p++;
    }

    /* 4. Skip separating whitespace between command and arguments */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    const char *args = p;

    /*
     * 5. Enforce command name boundary (MAX_COMMAND_NAME = 32).
     * If the token is >= 32 characters, reject it as an unknown command
     * without buffer overflow or silent truncation.
     */
    if (token_len >= MAX_COMMAND_NAME) {
        vga_puts("Unknown command: ");
        for (size_t i = 0; i < token_len && i < 64; i++) {
            vga_putc(token_start[i]);
        }
        if (token_len >= 64) {
            vga_puts("...");
        }
        vga_putc('\n');
        return;
    }

    /* Store command name into bounded local buffer */
    char cmd[MAX_COMMAND_NAME];
    for (size_t i = 0; i < token_len; i++) {
        cmd[i] = token_start[i];
    }
    cmd[token_len] = '\0';

    /* 6. Lookup in command table */
    for (size_t i = 0; commands[i].name != NULL; i++) {
        if (kstrcmp(cmd, commands[i].name) == 0) {
            commands[i].handler(args);
            return;
        }
    }

    /* 7. Command not found */
    vga_puts("Unknown command: ");
    vga_puts(cmd);
    vga_putc('\n');
}
