#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* I/O Port addresses for Master and Slave 8259 PICs */
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

/* End of Interrupt command code */
#define PIC_EOI      0x20

/* Re-mapped interrupt vector bases */
#define PIC1_OFFSET  0x20  /* Master PIC vectors: 0x20..0x27 (32..39) */
#define PIC2_OFFSET  0x28  /* Slave PIC vectors:  0x28..0x2F (40..47) */

/* Function prototypes */
void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);

#endif /* PIC_H */
