#include "vga.h"

/*
 * The VGA text mode buffer is physically mapped at 0xB8000.
 * In our identity-mapped page table, virtual address 0xB8000 points directly
 * to this physical framebuffer memory.
 */
static volatile uint16_t * const VGA_BUFFER = (volatile uint16_t *)0xB8000;

static size_t vga_row = 0;
static size_t vga_col = 0;
static uint8_t vga_current_color = 0x07; /* Light grey on black default */

void vga_set_color(uint8_t color) {
    vga_current_color = color;
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_current_color);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = blank;
        }
    }
    vga_row = 0;
    vga_col = 0;
}

void vga_init(void) {
    vga_current_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
}

/* Scroll the text buffer upwards by one row when reaching the bottom */
static void vga_scroll(void) {
    uint16_t blank = vga_entry(' ', vga_current_color);

    /* Move rows 1..(VGA_HEIGHT-1) up to rows 0..(VGA_HEIGHT-2) */
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y + 1) * VGA_WIDTH + x];
        }
    }

    /* Clear the last row */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    vga_row = VGA_HEIGHT - 1;
}

static void vga_newline(void) {
    vga_col = 0;
    if (++vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
}

void vga_backspace(void) {
    if (vga_col > 0) {
        vga_col--;
        VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_current_color);
    }
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_newline();
        return;
    }
    if (c == '\r') {
        vga_col = 0;
        return;
    }
    if (c == '\b') {
        vga_backspace();
        return;
    }
    if (c == '\t') {
        /* Align to next 4-space tab stop */
        size_t tab_spaces = 4 - (vga_col % 4);
        while (tab_spaces-- > 0) {
            vga_putc(' ');
        }
        return;
    }

    VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry((unsigned char)c, vga_current_color);
    if (++vga_col >= VGA_WIDTH) {
        vga_newline();
    }
}

void vga_puts(const char *str) {
    if (!str) {
        return;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putc(str[i]);
    }
}
