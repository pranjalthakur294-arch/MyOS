#include "timer.h"
#include "io.h"
#include "pic.h"

/*
 * Monotonically increasing 64-bit counter tracking elapsed timer ticks.
 * Incremented once per IRQ0 interrupt (~100 times per second).
 */
static volatile uint64_t timer_ticks = 0;

/*
 * timer_init - Programs the 8253/8254 PIT Channel 0 and unmasks IRQ0.
 *
 * Command byte breakdown (0x36):
 *   Bits 7..6 = 00  (Channel 0)
 *   Bits 5..4 = 11  (Access mode: lobyte/hibyte)
 *   Bits 3..1 = 011 (Operating Mode 3: Square Wave Generator)
 *   Bit 0     = 0   (Binary 16-bit counter mode)
 */
void timer_init(void) {
    /* 1. Send Command Word 0x36 to PIT Command Register (0x43) */
    outb(PIT_COMMAND, 0x36);
    io_wait();

    /* 2. Write 16-bit divisor to Channel 0 data port (0x40): low byte first */
    outb(PIT_CHANNEL0, (uint8_t)(PIT_DIVISOR & 0xFF));
    io_wait();

    /* 3. Write divisor high byte second */
    outb(PIT_CHANNEL0, (uint8_t)((PIT_DIVISOR >> 8) & 0xFF));
    io_wait();

    /* 4. Unmask IRQ0 on the 8259 Master PIC to allow timer interrupts */
    pic_clear_mask(0);
}

/*
 * timer_handler - C handler invoked on every IRQ0 (vector 0x20).
 * Increments tick counter and sends End of Interrupt (EOI) to Master PIC.
 */
void timer_handler(void) {
    timer_ticks++;
    pic_send_eoi(0);
}

/*
 * timer_get_ticks - Returns current total number of timer ticks since boot.
 */
uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

/*
 * timer_get_uptime_ms - Converts ticks to elapsed milliseconds.
 * At 100 Hz, 1 tick = 10 ms.
 */
uint64_t timer_get_uptime_ms(void) {
    return (timer_ticks * 1000) / TIMER_FREQUENCY_HZ;
}
