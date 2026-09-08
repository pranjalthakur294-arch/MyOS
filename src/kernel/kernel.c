#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "terminal.h"
#include "shell.h"
#include "timer.h"
#include "multiboot.h"
#include "pmm.h"
#include "vmm.h"

/* Assembly ISR stubs defined in interrupts.S */
extern void isr_timer(void);
extern void isr_keyboard(void);
extern void isr_page_fault(void);

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

    /* 3. Install Page Fault exception handler on vector 14 (#PF) */
    idt_set_gate(IDT_PAGE_FAULT_VECTOR, isr_page_fault, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 4. Initialize & remap PIC (all IRQs masked initially) */
    pic_init();

    /* 5. Install timer ISR on vector 0x20 (IRQ0) */
    idt_set_gate(IDT_TIMER_VECTOR, isr_timer, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 6. Install keyboard ISR on vector 0x21 (IRQ1) */
    idt_set_gate(IDT_KEYBOARD_VECTOR, isr_keyboard, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* 7. Initialize PIT timer subsystem (configures Mode 3 @ 100 Hz, unmasks IRQ0) */
    timer_init();
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] PIT timer initialized at 100 Hz (IRQ0 / vector 0x20)\n");

    /* 8. Initialize keyboard subsystem (unmasks IRQ1 on Master PIC) */
    keyboard_init();
    vga_puts("[OK] PS/2 keyboard initialized on IRQ1 (vector 0x21)\n\n");

    /* 9. Initialize shell subsystem */
    shell_init();

    /* 10. Initialize Physical Memory Manager from Multiboot memory map */
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[WARN] Non-standard bootloader magic received\n");
    }
    pmm_init(multiboot_info_addr);

    /* 11. Initialize Virtual Memory Manager */
    vmm_init();

    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Stage 5B Goal Achieved: 4 KiB virtual page mapping active!\n\n");

    /* 10. Initialize terminal subsystem (prints initial MyOS> prompt) */
    terminal_init();

    /* 11. Enable maskable hardware interrupts */
    __asm__ volatile ("sti");

    /* 12. Halt loop: Put CPU into low-power halt state waiting for interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
