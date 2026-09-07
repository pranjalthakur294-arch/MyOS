#include "pic.h"
#include "io.h"

/*
 * pic_init - Remaps the 8259 Master and Slave PICs out of the CPU exception range.
 *
 * Defaults:
 *   Master PIC was at 0x08..0x0F (colliding with CPU exceptions like Double Fault)
 *   Slave PIC was at 0x70..0x77
 *
 * Remapped to:
 *   Master PIC -> vectors 0x20..0x27 (32..39)
 *   Slave PIC  -> vectors 0x28..0x2F (40..47)
 */
void pic_init(void) {
    /* ICW1: Start initialization sequence in cascade mode */
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* ICW2: Set vector offsets */
    outb(PIC1_DATA, PIC1_OFFSET);  /* Master: 0x20 */
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);  /* Slave:  0x28 */
    io_wait();

    /* ICW3: Tell Master PIC there is a slave PIC at IRQ2 */
    outb(PIC1_DATA, 0x04);
    io_wait();
    /* ICW3: Tell Slave PIC its cascade identity (2) */
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: Set 8086/88 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /*
     * Mask all IRQs for Stage 2A.
     * Masking all lines (0xFF) prevents unexpected hardware interrupts
     * (such as the PIT timer IRQ0) from firing before handlers are configured.
     */
    outb(PIC1_DATA, 0xFF);
    io_wait();
    outb(PIC2_DATA, 0xFF);
    io_wait();
}

/*
 * pic_send_eoi - Informs the PIC that the interrupt has been serviced.
 *
 * Parameters:
 *   irq - IRQ number (0..15)
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/*
 * pic_set_mask - Disables a specific IRQ line on the PIC.
 */
void pic_set_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq_line -= 8;
    }
    value = inb(port) | (1 << irq_line);
    outb(port, value);
}

/*
 * pic_clear_mask - Enables a specific IRQ line on the PIC.
 */
void pic_clear_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq_line -= 8;
    }
    value = inb(port) & ~(1 << irq_line);
    outb(port, value);
}
