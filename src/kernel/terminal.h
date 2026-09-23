#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

/* Maximum capacity of the terminal line input buffer */
#define TERMINAL_BUFFER_SIZE 256

/* Public Terminal API */
void terminal_init(void);
void terminal_putc(char c);
void terminal_print_prompt(void);

/* VFS Terminal Device Operations (Stage 14A) */
vfs_node_t *terminal_get_vfs_node(void);
int terminal_read(void *buf, size_t count, size_t *bytes_read);
void terminal_cancel_reader(void *proc_ptr);

#endif /* TERMINAL_H */
