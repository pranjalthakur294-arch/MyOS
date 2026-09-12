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
#include "heap.h"
#include "task.h"
#include "scheduler.h"
#include "gdt.h"
#include "user.h"
#include "process.h"
#include "elf.h"
#include "vfs.h"
#include "ramfs.h"
#include "file.h"
#include "ata.h"
#include "block.h"
#include "pfs.h"

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
    vga_puts("================  MyOS - Educational x86-64 Kernel  ================\n");

    /* Stage 1 Checkpoint */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Long Mode & 64-bit kernel initialized\n");

    /* 2. Initialize IDT */
    idt_init();

    /* 2b. Initialize GDT & TSS (Ring 0/3 descriptors, dedicated RSP0 stack) */
    gdt_init();

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
    vga_puts("[OK] PS/2 keyboard initialized on IRQ1 (vector 0x21)\n");

    /* 9. Initialize shell subsystem */
    shell_init();

    /* 10. Initialize Physical Memory Manager from Multiboot memory map */
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[WARN] Non-standard bootloader magic\n");
    }
    pmm_init(multiboot_info_addr);

    /* 11. Initialize Virtual Memory Manager */
    vmm_init();

    /* 12. Initialize Kernel Heap */
    heap_init();

    /* 12a. Initialize Virtual File System and RAMFS (Stage 11A) */
    vfs_init();
    vfs_mount_root(ramfs_create_fs());

    /* 12a-2. Initialize File Descriptors Subsystem (Stage 11B) */
    file_init();

    /* 12a-3. Initialize ATA PIO Disk Driver (Stage 12A - silent probe) */
    ata_init();

    /* 12a-4. Initialize Generic Block Subsystem & ATA Adapter (Stage 12B) */
    block_init();
    ata_block_register();

    /* 12a-5. Initialize Persistent Filesystem Subsystem (Stage 12C - silent probe) */
    pfs_init();
    pfs_mount(block_get("ata0"));

    /* 12b. Initialize User Mode Subsystem */
    user_init();

    /* 13. Initialize Kernel Task Subsystem */
    task_init();

    /* 13b. Initialize Process Management Subsystem (Stage 9) */
    process_init();

    /* 13c. Initialize ELF Loader Subsystem (Stage 10) */
    elf_init();

    /* 14. Run Manual Cooperative Context Switch Demonstration */
    task_run_demo();

    /* 15. Initialize Round-Robin Scheduler and Preemptive Tasks */
    scheduler_init();

    vga_puts("Scheduler demo: ");

    /* Enable maskable hardware interrupts to start timer-driven preemption */
    __asm__ volatile ("sti");

    /* Wait for demonstration tasks to complete their initial visible rounds */
    while (!scheduler_is_demo_complete()) {
        __asm__ volatile ("hlt");
    }
    vga_putc('\n');

    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Stage 5B Goal Achieved: 4 KiB virtual page mapping active!\n");
    vga_puts("Stage 6 Goal Achieved: Dynamic kernel heap allocator active!\n");
    vga_puts("Stage 7A Goal Achieved: Manual context switching active!\n");
    vga_puts("Stage 7B Goal Achieved: Timer-driven round-robin scheduler active!\n");
    vga_puts("Stage 8A Goal Achieved: User mode Ring 3 foundation active!\n");
    vga_puts("Stage 8B Goal Achieved: System call subsystem (int 0x80) active!\n");

    /* 16. Initialize terminal subsystem (prints initial MyOS> prompt) */
    terminal_init();

    /* 17. Main kernel loop (Task 0): wait for interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
