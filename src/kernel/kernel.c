#include "vga.h"
#include "idt.h"
#include "pic.h"

/* Assembly ISR stub defined in interrupts.S */
extern void isr_test_wrapper(void);

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
    /* Initialize the 80x25 VGA text-mode display buffer */
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

    /* Stage 2A: Interrupt Foundation Initialization */
    idt_init();
    pic_init();

    /* Install test ISR wrapper for interrupt vector 0x20 */
    idt_set_gate(0x20, isr_test_wrapper, 0x08, IDT_FLAG_INTERRUPT_GATE);

    /* Trigger the test software interrupt to prove IDT dispatch works */
    __asm__ volatile ("int $0x20");

    /* Enable maskable hardware interrupts */
    __asm__ volatile ("sti");

    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("\nStage 2A Goal Achieved: IDT loaded & interrupt dispatch verified!\n\n");

    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_puts("CPU entering idle halt loop (waiting for interrupts)...\n");

    /* Halt loop: Put CPU into low-power halt state waiting for interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
