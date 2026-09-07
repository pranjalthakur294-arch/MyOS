#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>
#include <stdint.h>

/* Maximum capacity of the terminal line input buffer */
#define TERMINAL_BUFFER_SIZE 256

/* Public Terminal API */
void terminal_init(void);
void terminal_putc(char c);
void terminal_print_prompt(void);

#endif /* TERMINAL_H */
