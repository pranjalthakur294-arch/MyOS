/*
 * gdt.c - Global Descriptor Table (GDT) and Task State Segment (TSS) Management
 *
 * Establishes the 64-bit GDT with user-mode descriptors and initializes the
 * 64-bit Task State Segment (TSS) required for Ring 3 -> Ring 0 transitions.
 */

#include "gdt.h"
#include "vga.h"
#include <stddef.h>

/*
 * 7-entry Global Descriptor Table (56 bytes total)
 *   0x00: Null Descriptor
 *   0x08: 64-bit Kernel Code (DPL 0)
 *   0x10: 64-bit Kernel Data (DPL 0)
 *   0x18: 64-bit User Data   (DPL 3)
 *   0x20: 64-bit User Code   (DPL 3)
 *   0x28: 64-bit Available TSS (Low 8 bytes)
 *   0x30: 64-bit Available TSS (High 8 bytes)
 */
static uint64_t gdt[7];
static gdt_ptr_t gdt_pointer;

/* Static 64-bit Task State Segment instance */
static tss_t kernel_tss;

/* Dedicated kernel stack for Ring 3 -> Ring 0 hardware interrupt transitions */
static uint8_t user_kernel_stack[4096] __attribute__((aligned(16)));

/*
 * gdt_init - Configures and loads the 64-bit GDT and TSS.
 */
void gdt_init(void) {
    /* Entry 0: Null Descriptor */
    gdt[0] = 0x0000000000000000ULL;

    /* Entry 1 (0x08): 64-bit Kernel Code Segment (DPL 0, Access 0x9A, Flags 0x20) */
    gdt[1] = 0x00209A0000000000ULL;

    /* Entry 2 (0x10): 64-bit Kernel Data Segment (DPL 0, Access 0x92, Flags 0x00) */
    gdt[2] = 0x0000920000000000ULL;

    /* Entry 3 (0x18): 64-bit User Data Segment (DPL 3, Access 0xF2, Flags 0x00) */
    gdt[3] = 0x0000F20000000000ULL;

    /* Entry 4 (0x20): 64-bit User Code Segment (DPL 3, Access 0xFA, Flags 0x20) */
    gdt[4] = 0x0020FA0000000000ULL;

    /* Initialize Task State Segment (TSS) */
    uint8_t *tss_bytes = (uint8_t *)&kernel_tss;
    for (size_t i = 0; i < sizeof(tss_t); i++) {
        tss_bytes[i] = 0;
    }

    /* Configure RSP0 to top of dedicated kernel stack */
    kernel_tss.rsp0 = (uint64_t)(user_kernel_stack + sizeof(user_kernel_stack));
    /* Disallow user I/O ports by setting IOPB offset to TSS limit */
    kernel_tss.iopb_offset = (uint16_t)sizeof(tss_t);

    /* Construct 16-byte 64-bit TSS descriptor */
    uint64_t base  = (uint64_t)&kernel_tss;
    uint32_t limit = (uint32_t)(sizeof(tss_t) - 1); /* 103 bytes */

    /* Low 8 bytes (Selector 0x28) */
    uint64_t tss_low = (uint64_t)(limit & 0xFFFF);
    tss_low |= ((base & 0x00FFFFFFULL) << 16);
    tss_low |= (0x89ULL << 40); /* Present=1, DPL=0, S=0 (System), Type=9 (Available 64-bit TSS) */
    tss_low |= (((uint64_t)(limit >> 16) & 0x0FULL) << 48);
    tss_low |= (((base >> 24) & 0xFFULL) << 56);

    /* High 8 bytes (Selector 0x30) */
    uint64_t tss_high = (base >> 32) & 0xFFFFFFFFULL;

    gdt[5] = tss_low;
    gdt[6] = tss_high;

    /* Prepare GDTR descriptor pointer */
    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base  = (uint64_t)&gdt;

    /* Load GDTR, reload data segment registers, and load TR */
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "r"(&gdt_pointer)
        : "rax", "memory"
    );
}

/*
 * gdt_set_rsp0 - Updates the TSS RSP0 pointer for privilege transitions.
 */
void gdt_set_rsp0(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}

/*
 * gdt_get_rsp0 - Returns current TSS RSP0 pointer.
 */
uint64_t gdt_get_rsp0(void) {
    return kernel_tss.rsp0;
}

/*
 * gdt_get_tss - Returns pointer to static TSS instance.
 */
const tss_t *gdt_get_tss(void) {
    return &kernel_tss;
}

/*
 * gdt_print_info - Displays formatted GDT and TSS configuration for shell.
 */
void gdt_print_info(void) {
    vga_puts("\nGDT & TSS Configuration:\n");
    vga_puts("  GDT Base:         ");
    vga_print_hex((uint64_t)&gdt);
    vga_puts("\n  GDT Limit:        ");
    vga_print_dec(sizeof(gdt) - 1);
    vga_puts(" bytes (7 entries)\n");
    vga_puts("  Kernel CS:        0x0008 (DPL 0, 64-bit Code)\n");
    vga_puts("  Kernel DS:        0x0010 (DPL 0, 64-bit Data)\n");
    vga_puts("  User DS:          0x0018 (DPL 3, Selector 0x001B)\n");
    vga_puts("  User CS:          0x0020 (DPL 3, Selector 0x0023)\n");
    vga_puts("  TSS Selector:     0x0028 (64-bit Available TSS)\n");
    vga_puts("  TSS Base:         ");
    vga_print_hex((uint64_t)&kernel_tss);
    vga_puts("\n  TSS Limit:        ");
    vga_print_dec(sizeof(tss_t) - 1);
    vga_puts(" bytes\n");
    vga_puts("  TSS RSP0:         ");
    vga_print_hex(kernel_tss.rsp0);
    vga_putc('\n');
}
