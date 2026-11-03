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

int main(int argc, char **argv) {
    (void)ls_data;

    const char *target = ".";
    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        target = argv[0];
    }

    int dfd = open(target, O_RDONLY);
    if (dfd < 0) {
        print_err("ls: cannot open directory: ");
        print_err(target);
        print_err("\n");
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
