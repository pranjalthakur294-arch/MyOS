/*
 * user/pwd.c - Userland Print Working Directory Utility
 *
 * Retrieves the current working directory via the getcwd() syscall
 * and writes it followed by a newline to standard output.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

volatile uint64_t pwd_data = 0xA1B2C3D4E5F60718ULL;

int64_t user_main(void) {
    (void)pwd_data;

    char cwd_buf[256];
    memset(cwd_buf, 0, sizeof(cwd_buf));

    char *res = getcwd(cwd_buf, sizeof(cwd_buf));
    if (res == NULL) {
        print_err("pwd: failed to retrieve current working directory\n");
        return 1;
    }

    puts(cwd_buf);
    return 0;
}
