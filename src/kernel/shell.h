#ifndef SHELL_H
#define SHELL_H

/* Maximum allowable length of command name (including null terminator) */
#define MAX_COMMAND_NAME 32

/*
 * shell_init - Initializes the shell subsystem.
 * Called once during kernel bootstrap before hardware interrupts are enabled.
 */
void shell_init(void);

/*
 * shell_execute - Parses and executes a command line string.
 *
 * Parameters:
 *   cmd_line: Null-terminated string containing the user command and arguments.
 *             Passed from the terminal input buffer upon Enter keypress.
 */
void shell_execute(const char *cmd_line);

#endif /* SHELL_H */
