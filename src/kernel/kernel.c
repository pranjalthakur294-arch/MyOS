#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "terminal.h"
#include "shell.h"
#include "timer.h"
#include "multiboot.h"
#include "pmm.h"

/* Assembly ISR stubs defined in interrupts.S */
extern void isr_timer(void);
extern void isr_keyboard(void);

/*
 * kernel_main - C entry point of MyOS
 * Receives Multiboot magic in RDI and Multiboot information pointer in RSI.
 */
void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info_addr) {
    /* 1. Initialize the 80x25 VGA text-mode display buffer */
    vga_init();

    /* Header Banner */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("================================================================\n");
    vga_puts("             MyOS - Educational x86-64 Kernel                   \n");
    vga_puts("================================================================\n\n");

    /* Stage 1 Checkpoints */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Multiboot bootloader handoff completed\n");
    vga_puts("[OK] Switched from 32-bit Protected Mode to 64-bit Long Mode\n");
    vga_puts("[OK] 4-level paging initialized (1 GiB identity-mapped)\n");
    vga_puts("[OK] 64-bit Global Descriptor Table (GDT64) loaded\n");
    vga_puts("[OK] 16 KiB 64-bit kernel stack established\n");
    vga_puts("[OK] Transferred execution to C kernel_main()\n\n");

    /* 2. Initialize IDT */
    idt_init();

    /* 3. Initialize & remap PIC (all IRQs masked initially) */
    pic_init();

    /* 4. Install timer ISR on vector 0x20 (IRQ0) */
    idt_set_gate(IDT_TIMER_VECTOR, isr_timer, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 5. Install keyboard ISR on vector 0x21 (IRQ1) */
    idt_set_gate(IDT_KEYBOARD_VECTOR, isr_keyboard, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 6. Initialize PIT timer subsystem (configures Mode 3 @ 100 Hz, unmasks IRQ0) */
    timer_init();
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] PIT timer initialized at 100 Hz (IRQ0 / vector 0x20)\n");

    /* 7. Initialize keyboard subsystem (unmasks IRQ1 on Master PIC) */
    keyboard_init();
    vga_puts("[OK] PS/2 keyboard initialized on IRQ1 (vector 0x21)\n\n");

    /* 8. Initialize shell subsystem */
    shell_init();

    /* 9. Initialize Physical Memory Manager from Multiboot memory map */
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[WARN] Non-standard bootloader magic received\n");
    }
    pmm_init(multiboot_info_addr);

    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Stage 5A Goal Achieved: 4 KiB frame allocator active!\n\n");

    /* 10. Initialize terminal subsystem (prints initial MyOS> prompt) */
    terminal_init();

    /* 11. Enable maskable hardware interrupts */
    __asm__ volatile ("sti");

    /* 12. Halt loop: Put CPU into low-power halt state waiting for interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
