#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* PS/2 Controller I/O Ports */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_COMMAND_PORT 0x64

/* Public keyboard functions */
void keyboard_init(void);
void keyboard_handler(void);

#endif /* KEYBOARD_H */
