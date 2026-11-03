/*
 * user/mkdir.c - Userland Directory Creation Utility
 *
 * Invokes the mkdir() syscall on the target directory path passed in argv[0].
 * Reports clear error messages on failure.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 1 || argv[0] == NULL || argv[0][0] == '\0') {
        print_err("Usage: mkdir <directory>\n");
        return 1;
    }

    int rc = mkdir(argv[0]);
    if (rc < 0) {
        if (rc == -EEXIST) {
            print_err("mkdir: cannot create directory '");
            print_err(argv[0]);
            print_err("': File exists\n");
        } else if (rc == -ENOENT) {
            print_err("mkdir: cannot create directory '");
            print_err(argv[0]);
            print_err("': No such file or directory\n");
        } else if (rc == -ENOTDIR) {
            print_err("mkdir: cannot create directory '");
            print_err(argv[0]);
            print_err("': Not a directory\n");
        } else {
            print_err("mkdir: failed to create directory '");
            print_err(argv[0]);
            print_err("'\n");
        }
        return 2;
    }

    return 0;
}
