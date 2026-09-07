#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/*
 * Legacy 8253/8254 Programmable Interval Timer (PIT) I/O Ports
 */
#define PIT_CHANNEL0 0x40  /* Channel 0 data port (connected to IRQ0) */
#define PIT_COMMAND  0x43  /* Mode/Command register port */

/*
 * PIT Timing Specifications
 * Standard input clock oscillator base frequency is ~1.193182 MHz (1193182 Hz).
 * Target frequency is 100 Hz (1 tick ≈ 10 ms).
 * Divisor = 1193182 / 100 = 11931.
 * Actual frequency = 1193182 / 11931 ≈ 100.00687 Hz.
 */
#define PIT_BASE_FREQUENCY 1193182
#define TIMER_FREQUENCY_HZ 100
#define PIT_DIVISOR        (PIT_BASE_FREQUENCY / TIMER_FREQUENCY_HZ)

/* Public Timer API */
void timer_init(void);
void timer_handler(void);
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_ms(void);

#endif /* TIMER_H */
