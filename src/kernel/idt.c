#include "idt.h"

/* 256-entry Interrupt Descriptor Table */
static struct idt_entry idt[IDT_ENTRIES];

/* Pointer descriptor loaded into IDTR */
static struct idt_ptr idtr;

/*
 * idt_load - Loads the IDTR register with the base and limit of our IDT table.
 */
static inline void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

/*
 * idt_set_gate - Configures a specific entry in the IDT.
 *
 * Parameters:
 *   vector   - Interrupt vector number (0..255)
 *   handler  - Function pointer to the ISR entry stub
 *   selector - GDT code segment selector (0x08)
 *   flags    - Type and attribute flags (e.g. IDT_FLAG_INTERRUPT_GATE = 0x8E)
 */
void idt_set_gate(uint8_t vector, void (*handler)(void), uint16_t selector, uint8_t flags) {
    uint64_t addr = (uint64_t)handler;

    idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector    = selector;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = flags;
    idt[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved    = 0;
}

/*
 * idt_init - Initializes all 256 IDT entries to null and loads the IDTR register.
 */
void idt_init(void) {
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)&idt;

    /* Initialize all 256 gates to 0 */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    /* Load the IDT using the lidt instruction */
    idt_load();
}
