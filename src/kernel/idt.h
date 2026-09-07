#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Total number of IDT entries supported by x86 architecture */
#define IDT_ENTRIES 256

/*
 * 16-byte x86-64 Interrupt Descriptor Gate Structure
 * In Long Mode, each gate descriptor is expanded to 16 bytes to hold
 * a full 64-bit target ISR virtual address.
 */
struct idt_entry {
    uint16_t offset_low;   /* Target ISR address bits 0..15 */
    uint16_t selector;     /* Code segment selector in GDT (0x08) */
    uint8_t  ist;          /* Bits 0..2: IST index (0 = none), Bits 3..7: Reserved (0) */
    uint8_t  type_attr;    /* Type and attributes (e.g. 0x8E = 64-bit Interrupt Gate, DPL 0, Present 1) */
    uint16_t offset_mid;   /* Target ISR address bits 16..31 */
    uint32_t offset_high;  /* Target ISR address bits 32..63 */
    uint32_t reserved;     /* Reserved (must be 0) */
} __attribute__((packed));

/*
 * 10-byte IDTR register structure loaded via the lidt instruction.
 */
struct idt_ptr {
    uint16_t limit;        /* Size of IDT in bytes minus 1 (256 * 16 - 1 = 4095) */
    uint64_t base;         /* 64-bit linear base address of the IDT */
} __attribute__((packed));

/* Common IDT gate flags */
#define IDT_FLAG_INTERRUPT_GATE 0x8E  /* Present=1, DPL=0, Type=0xE (64-bit Interrupt Gate) */
#define IDT_FLAG_TRAP_GATE      0x8F  /* Present=1, DPL=0, Type=0xF (64-bit Trap Gate) */

/* Function prototypes */
void idt_set_gate(uint8_t vector, void (*handler)(void), uint16_t selector, uint8_t flags);
void idt_init(void);

#endif /* IDT_H */
