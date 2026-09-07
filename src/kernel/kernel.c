#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "terminal.h"

/* Assembly ISR stubs defined in interrupts.S */
extern void isr_test_wrapper(void);
extern void isr_keyboard(void);

/*
 * test_interrupt_handler - C handler called by isr_test_wrapper
 * Invoked when test interrupt 0x20 fires.
 */
void test_interrupt_handler(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Interrupt subsystem initialized (test interrupt handled)\n");
}

/*
 * kernel_main - C entry point of MyOS
 */
void kernel_main(void) {
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

    /* 3. Initialize & remap PIC */
    pic_init();

    /* Stage 2A verification: Install and trigger software test interrupt 0x20 */
    idt_set_gate(0x20, isr_test_wrapper, 0x08, IDT_FLAG_INTERRUPT_GATE);
    __asm__ volatile ("int $0x20");

    /* 4. Install keyboard ISR on vector 0x21 (IRQ1) */
    idt_set_gate(0x21, isr_keyboard, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 5. Initialize keyboard subsystem (unmasks IRQ1 on Master PIC) */
    keyboard_init();
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] PS/2 keyboard initialized on IRQ1 (vector 0x21)\n\n");

    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Stage 3A Goal Achieved: Terminal discipline active!\n\n");

    /* 6. Initialize terminal subsystem (prints initial MyOS> prompt) */
    terminal_init();

    /* 7. Enable maskable hardware interrupts */
    __asm__ volatile ("sti");

    /* 8. Halt loop: Put CPU into low-power halt state waiting for keyboard interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
