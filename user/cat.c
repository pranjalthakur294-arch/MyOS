/*
 * user/cat.c - Userland Concatenate & Print Utility
 *
 * Opens the file specified in argv[0], reads its contents using a bounded buffer,
 * writes to standard output, and closes the file descriptor on both success and failure.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 1 || argv[0] == NULL || argv[0][0] == '\0') {
        print_err("Usage: cat <filename>\n");
        return 1;
    }

    int fd = open(argv[0], O_RDONLY);
    if (fd < 0) {
        print_err("cat: cannot open file: ");
        print_err(argv[0]);
        print_err("\n");
        return 2;
    }

    char buf[128];
    int64_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        int64_t written = 0;
        while (written < n) {
            int64_t w = write(STDOUT_FILENO, buf + written, (size_t)(n - written));
            if (w <= 0) {
                print_err("cat: write error\n");
                close(fd);
                return 3;
            }
            written += w;
        }
    }

    if (n < 0) {
        print_err("cat: read error\n");
        close(fd);
        return 4;
    }

    close(fd);
    return 0;
}
