#ifndef IO_H
#define IO_H

#include <stdint.h>

/*
 * inb - Reads an 8-bit value from an I/O port.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*
 * outb - Writes an 8-bit value to an I/O port.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/*
 * io_wait - Provides a brief delay for slow I/O devices (such as the legacy 8259 PIC)
 * by writing to the unused port 0x80 (traditionally used for POST debug codes).
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* IO_H */
