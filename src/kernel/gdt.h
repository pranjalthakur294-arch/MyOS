#ifndef GDT_H
#define GDT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * GDT Segment Selectors (Byte Offsets in GDT)
 */
#define GDT_KERNEL_CS  0x08
#define GDT_KERNEL_DS  0x10
#define GDT_USER_DS    0x18
#define GDT_USER_CS    0x20
#define GDT_TSS        0x28

/*
 * Segment Selectors with Requested Privilege Level (RPL)
 * In x86-64, bits 0-1 of a segment selector represent the RPL.
 * Ring 0 uses RPL 0, Ring 3 uses RPL 3.
 */
#define SELECTOR_KERNEL_CS (GDT_KERNEL_CS | 0)  /* 0x08 */
#define SELECTOR_KERNEL_DS (GDT_KERNEL_DS | 0)  /* 0x10 */
#define SELECTOR_USER_DS   (GDT_USER_DS   | 3)  /* 0x1B */
#define SELECTOR_USER_CS   (GDT_USER_CS   | 3)  /* 0x23 */
#define SELECTOR_TSS       (GDT_TSS       | 0)  /* 0x28 */

/*
 * x86-64 Task State Segment (TSS) Structure
 * Per Intel SDM Vol 3A §7.7 and AMD64 APM Vol 2 §12.2.
 * Total size: exactly 104 bytes (0x68 bytes).
 */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;       /* Stack pointer for Ring 0 (CPL 0) interrupt return */
    uint64_t rsp1;       /* Stack pointer for Ring 1 (unused) */
    uint64_t rsp2;       /* Stack pointer for Ring 2 (unused) */
    uint64_t reserved1;
    uint64_t ist1;       /* Interrupt Stack Table 1 */
    uint64_t ist2;       /* Interrupt Stack Table 2 */
    uint64_t ist3;       /* Interrupt Stack Table 3 */
    uint64_t ist4;       /* Interrupt Stack Table 4 */
    uint64_t ist5;       /* Interrupt Stack Table 5 */
    uint64_t ist6;       /* Interrupt Stack Table 6 */
    uint64_t ist7;       /* Interrupt Stack Table 7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;/* I/O Permission Bitmap Base Address */
} tss_t;

/* GDT Pointer structure loaded into GDTR by LGDT */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/*
 * GDT & TSS Public Subsystem API
 */
void gdt_init(void);
void gdt_set_rsp0(uint64_t rsp0);
uint64_t gdt_get_rsp0(void);
const tss_t *gdt_get_tss(void);
void gdt_print_info(void);

#endif /* GDT_H */
