/*
 * user/ls.c - Userland Directory Listing Utility
 *
 * Opens the current working directory (".") and iterates through directory
 * entries using the readdir() syscall wrapper. Outputs formatted entry type
 * and name to standard output.
 */

#include "unistd.h"
#include "dirent.h"
#include "stdio.h"
#include "string.h"

volatile uint64_t ls_data = 0x55AA55AA12345678ULL;

int64_t user_main(void) {
    (void)ls_data;

    int dfd = open(".", O_RDONLY);
    if (dfd < 0) {
        print_err("ls: cannot open current directory\n");
        return 1;
    }

    struct dirent entry;
    int rc;

    while ((rc = readdir(dfd, &entry)) > 0) {
        if (entry.type == DT_DIR) {
            print("[DIR]  ");
        } else if (entry.type == DT_CHR) {
            print("[DEV]  ");
        } else {
            print("[FILE] ");
        }
        print(entry.name);
        print("\n");
    }

    if (rc < 0) {
        print_err("ls: error reading directory entries\n");
        close(dfd);
        return 2;
    }

    close(dfd);
    return 0;
}
