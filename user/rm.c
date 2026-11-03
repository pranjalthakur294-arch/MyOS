/*
 * user/rm.c - Userland File Removal Utility
 *
 * Invokes the unlink() syscall on the target file path passed in argv[0].
 * Reports clear error messages on failure. Does not support recursive directory removal.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 1 || argv[0] == NULL || argv[0][0] == '\0') {
        print_err("Usage: rm <filename>\n");
        return 1;
    }

    int rc = unlink(argv[0]);
    if (rc < 0) {
        if (rc == -ENOENT) {
            print_err("rm: cannot remove '");
            print_err(argv[0]);
            print_err("': No such file or directory\n");
        } else if (rc == -EISDIR) {
            print_err("rm: cannot remove '");
            print_err(argv[0]);
            print_err("': Is a directory\n");
        } else if (rc == -EPERM || rc == -EBUSY) {
            print_err("rm: cannot remove '");
            print_err(argv[0]);
            print_err("': Operation not permitted\n");
        } else {
            print_err("rm: failed to remove '");
            print_err(argv[0]);
            print_err("'\n");
        }
        return 2;
    }

    return 0;
}
