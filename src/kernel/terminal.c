#include "terminal.h"
#include "vga.h"

/* Fixed-size static line input buffer in BSS */
static char line_buffer[TERMINAL_BUFFER_SIZE];
static size_t line_len = 0;

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
 * terminal_init - Prepares the input buffer and prints the initial prompt.
 */
void terminal_init(void) {
    line_len = 0;
    line_buffer[0] = '\0';
    terminal_print_prompt();
}

/*
 * terminal_putc - Processes incoming characters through the line discipline.
 *
 * Enforces:
 *   - Enter ('\n'): Commits current line, moves cursor to next line,
 *     resets buffer, and displays a fresh prompt.
 *   - Backspace ('\b'): Only allows erasing user-entered characters.
 *     If line_len == 0, Backspace is safely dropped to protect the prompt.
 *   - Printable characters: Appends to line_buffer if space is available,
 *     guarantees null-termination, and echoes character to VGA.
 */
void terminal_putc(char c) {
    if (c == '\n') {
        vga_putc('\n');

        /*
         * Stage 3A: Finalize line buffer.
         * (Stage 3B will pass line_buffer to shell_execute() here).
         */
        line_buffer[line_len] = '\0';
        line_len = 0;
        line_buffer[0] = '\0';

        /* Print fresh prompt for next input */
        terminal_print_prompt();
        return;
    }

    if (c == '\b') {
        /* Prompt protection boundary */
        if (line_len > 0) {
            line_len--;
            line_buffer[line_len] = '\0';
            vga_backspace();
        }
        return;
    }

    /* Standard printable characters, numbers, symbols, spaces */
    if (line_len < TERMINAL_BUFFER_SIZE - 1) {
        line_buffer[line_len++] = c;
        line_buffer[line_len] = '\0';
        vga_putc(c);
    }
}
