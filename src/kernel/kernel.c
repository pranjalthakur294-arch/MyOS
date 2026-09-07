#include "vga.h"

/*
 * kernel_main - C entry point of MyOS
 *
 * Invoked by long_mode_start in boot.S after:
 * 1. CPU has been verified for CPUID and 64-bit Long Mode capability.
 * 2. 4-level paging (PML4, PDPT, PD) is configured and identity-mapped.
 * 3. 64-bit GDT has been activated and data segment registers initialized.
 * 4. 16 KiB kernel stack pointer (RSP) has been established and 16-byte aligned.
 */
void kernel_main(void) {
    /* Initialize the 80x25 VGA text-mode display buffer */
    vga_init();

    /* Header Banner */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("================================================================\n");
    vga_puts("             MyOS - Educational x86-64 Kernel                   \n");
    vga_puts("================================================================\n\n");

    /* Status Checkpoints */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Multiboot bootloader handoff completed\n");
    vga_puts("[OK] Switched from 32-bit Protected Mode to 64-bit Long Mode\n");
    vga_puts("[OK] 4-level paging initialized (1 GiB identity-mapped)\n");
    vga_puts("[OK] 64-bit Global Descriptor Table (GDT64) loaded\n");
    vga_puts("[OK] 16 KiB 64-bit kernel stack established\n");
    vga_puts("[OK] Transferred execution to C kernel_main()\n\n");

    /* Completion message */
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Stage 1 Goal Achieved: Minimal x86-64 bare-metal kernel booted!\n\n");

    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_puts("CPU entering idle halt loop (waiting for interrupts)...\n");

    /* Halt loop: Put CPU into low-power halt state */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
