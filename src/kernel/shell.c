#include "shell.h"
#include "vga.h"
#include "timer.h"
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
static void builtin_halt(const char *args);

/*
 * Static command table terminated with a sentinel {NULL, NULL, NULL}.
 */
static const struct shell_command commands[] = {
    {"help",   "Display list of available commands", builtin_help},
    {"clear",  "Clear the terminal screen",          builtin_clear},
    {"about",  "Display system information",          builtin_about},
    {"echo",   "Print arguments to the screen",       builtin_echo},
    {"uptime", "Show system uptime",                 builtin_uptime},
    {"halt",   "Halt the system (stops the CPU)",     builtin_halt},
    {NULL,     NULL,                                  NULL}
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
        for (size_t s = name_len; s < 7; s++) {
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
