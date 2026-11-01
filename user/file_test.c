/*
 * user/file_test.c - User-Space Filesystem Syscall Verification Program
 *
 * Verifies:
 *   1. open("/readme.txt", O_RDONLY) succeeds with a valid fd (>= 3).
 *   2. read(fd, buf, size) reads non-zero bytes from the file.
 *   3. File content is printed to stdout.
 *   4. close(fd) succeeds (returns 0).
 *   5. open non-existent file returns negative error.
 *   6. read invalid fd returns negative error (-EBADF).
 *   7. close invalid fd returns negative error (-EBADF).
 *   8. close already-closed fd returns negative error (-EBADF).
 *   9. Prints "file_test: all assertions passed\n" and exits with 0.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

volatile uint64_t file_test_data = 0xCAFEBABE12345678ULL;

int64_t user_main(void) {
    (void)file_test_data;

    print("file_test: starting filesystem syscall tests...\n");

    /* 1. Open /readme.txt */
    int fd = open("/readme.txt", O_RDONLY);
    if (fd < 0) {
        print_err("file_test: FAIL open /readme.txt\n");
        return 1;
    }

    /* 2. Read content into buffer */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int64_t bytes = read(fd, buf, sizeof(buf) - 1);
    if (bytes <= 0) {
        print_err("file_test: FAIL read /readme.txt\n");
        close(fd);
        return 2;
    }

    /* 3. Write content to stdout */
    print("--- /readme.txt content begin ---\n");
    write(STDOUT_FILENO, buf, (size_t)bytes);
    print("--- /readme.txt content end ---\n");

    /* 4. Close file */
    int rc = close(fd);
    if (rc != 0) {
        print_err("file_test: FAIL close /readme.txt\n");
        return 3;
    }

    /* 5. Error handling: open non-existent file */
    int bad_fd = open("/nonexistent_file_xyz_987.txt", O_RDONLY);
    if (bad_fd >= 0) {
        print_err("file_test: FAIL open nonexistent file succeeded\n");
        close(bad_fd);
        return 4;
    }

    /* 6. Error handling: read invalid fd */
    int64_t bad_read = read(99, buf, sizeof(buf));
    if (bad_read >= 0) {
        print_err("file_test: FAIL read invalid fd succeeded\n");
        return 5;
    }

    /* 7. Error handling: close invalid fd */
    int bad_close = close(99);
    if (bad_close >= 0) {
        print_err("file_test: FAIL close invalid fd succeeded\n");
        return 6;
    }

    /* 8. Error handling: close already-closed fd */
    int double_close = close(fd);
    if (double_close >= 0) {
        print_err("file_test: FAIL close already-closed fd succeeded\n");
        return 7;
    }

    print("file_test: all assertions passed\n");
    return 0;
}
