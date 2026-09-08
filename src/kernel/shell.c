#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
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
static void builtin_halt(const char *args);

/*
 * Static command table terminated with a sentinel {NULL, NULL, NULL}.
 */
static const struct shell_command commands[] = {
    {"help",     "List commands",         builtin_help},
    {"clear",    "Clear screen",          builtin_clear},
    {"about",    "System information",    builtin_about},
    {"echo",     "Print arguments",       builtin_echo},
    {"uptime",   "System uptime",         builtin_uptime},
    {"meminfo",  "Physical memory info",  builtin_meminfo},
    {"alloc",    "Allocate 4 KiB frame",  builtin_alloc},
    {"free",     "Free test frame",       builtin_free},
    {"vmmap",    "Virtual memory info",   builtin_vmmap},
    {"vmtest",   "Test virtual mapping",  builtin_vmtest},
    {"heapinfo", "Kernel heap info",      builtin_heapinfo},
    {"heaptest", "Test kernel heap",      builtin_heaptest},
    {"halt",     "Halt system",           builtin_halt},
    {NULL,       NULL,                    NULL}
};

/*
 * Built-in Command: help
 * Iterates through the command table and prints each command and description.
 */
static void builtin_help(const char *args) {
    (void)args;
    vga_puts("Available commands:\n");
    for (size_t i = 0; commands[i].name != NULL; i++) {
        vga_puts("  ");
        vga_puts(commands[i].name);
        size_t name_len = kstrlen(commands[i].name);
        for (size_t s = name_len; s < 10; s++) {
            vga_putc(' ');
        }
        vga_puts("- ");
        vga_puts(commands[i].description);
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
