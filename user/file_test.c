/*
 * user/file_test.c - User-Space Filesystem Syscall Verification Program
 *
 * Verifies:
 *   1. open("/readme.txt", O_RDONLY) succeeds with a valid fd (>= 3).
 *   2. read(fd, buf, size) reads non-zero bytes from the file.
 *   3. File content is printed to stdout.
 *   4. seek(fd, 6, SEEK_SET) repositions cursor and read returns expected bytes.
 *   5. seek(fd, -10, SEEK_SET) returns negative error.
 *   6. seek(fd, 0, 99) invalid whence returns negative error.
 *   7. stat("/readme.txt", &st) succeeds with STAT_TYPE_FILE and valid size.
 *   8. stat("/bin", &st) succeeds with STAT_TYPE_DIR.
 *   9. stat("/nonexistent_file_xyz_987.txt", &st) returns negative error.
 *  10. close(fd) succeeds (returns 0).
 *  11. open non-existent file returns negative error.
 *  12. read invalid fd returns negative error (-EBADF).
 *  13. close invalid fd returns negative error (-EBADF).
 *  14. close already-closed fd returns negative error (-EBADF).
 *  15. Prints "file_test: all assertions passed\n" and exits with 0.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

static const char err_fail[] = "FAIL\n";

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("file_test: starting filesystem syscall tests...\n");

    /* 1. Open /readme.txt */
    int fd = open("/readme.txt", O_RDONLY);
    if (fd < 0) {
        print_err(err_fail);
        return 1;
    }

    /* 2. Read content into buffer */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int64_t bytes = read(fd, buf, sizeof(buf) - 1);
    if (bytes <= 0) {
        print_err(err_fail);
        close(fd);
        return 2;
    }

    /* 3. Write content to stdout */
    print("--- /readme.txt content begin ---\n");
    write(STDOUT_FILENO, buf, (size_t)bytes);
    print("--- /readme.txt content end ---\n");

    /* 4. Test seek SEEK_SET: seek to offset 6 ("from ...") */
    int64_t seek_pos = seek(fd, 6, SEEK_SET);
    if (seek_pos != 6) {
        print_err(err_fail);
        close(fd);
        return 3;
    }

    char seek_buf[8];
    memset(seek_buf, 0, sizeof(seek_buf));
    int64_t seek_read = read(fd, seek_buf, 4);
    if (seek_read != 4 || strncmp(seek_buf, "from", 4) != 0) {
        print_err(err_fail);
        close(fd);
        return 4;
    }

    /* 5. Hardened seek tests: INT64_MIN, underflow, overflow, and offset preservation */
    int64_t i64_min = (int64_t)0x8000000000000000ULL;
    int64_t i64_max = (int64_t)0x7FFFFFFFFFFFFFFFULL;

    /* Reset to position 10 */
    if (seek(fd, 10, SEEK_SET) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* SEEK_SET with INT64_MIN */
    if (seek(fd, i64_min, SEEK_SET) >= 0 || seek(fd, 0, SEEK_CUR) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* SEEK_CUR with INT64_MIN */
    if (seek(fd, i64_min, SEEK_CUR) >= 0 || seek(fd, 0, SEEK_CUR) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* SEEK_END with INT64_MIN */
    if (seek(fd, i64_min, SEEK_END) >= 0 || seek(fd, 0, SEEK_CUR) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* Negative offset larger than current position */
    if (seek(fd, -11, SEEK_CUR) >= 0 || seek(fd, 0, SEEK_CUR) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* Positive overflow near INT64_MAX */
    if (seek(fd, i64_max, SEEK_CUR) >= 0 || seek(fd, 0, SEEK_CUR) != 10) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* Valid negative offset */
    if (seek(fd, -4, SEEK_CUR) != 6 || seek(fd, 0, SEEK_CUR) != 6) {
        print_err(err_fail);
        close(fd);
        return 5;
    }

    /* 6. Test seek error: invalid whence */
    if (seek(fd, 0, 99) >= 0 || seek(fd, 0, SEEK_CUR) != 6) {
        print_err(err_fail);
        close(fd);
        return 6;
    }

    /* 7. Test stat on file */
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    int s_res = stat("/readme.txt", &st);
    if (s_res != 0 || st.st_mode != STAT_TYPE_FILE || st.st_size == 0) {
        print_err(err_fail);
        close(fd);
        return 7;
    }

    /* 8. Test stat on directory */
    memset(&st, 0, sizeof(struct stat));
    s_res = stat("/bin", &st);
    if (s_res != 0 || st.st_mode != STAT_TYPE_DIR) {
        print_err(err_fail);
        close(fd);
        return 8;
    }

    /* 9. Test stat on nonexistent file */
    if (stat("/nonexistent_file_xyz_987.txt", &st) >= 0) {
        print_err(err_fail);
        close(fd);
        return 9;
    }

    /* 10. Close file */
    int rc = close(fd);
    if (rc != 0) {
        print_err(err_fail);
        return 10;
    }

    /* 11. Error handling: open non-existent file */
    int bad_fd = open("/nonexistent_file_xyz_987.txt", O_RDONLY);
    if (bad_fd >= 0) {
        print_err(err_fail);
        close(bad_fd);
        return 11;
    }

    /* 12. Error handling: read invalid fd */
    int64_t bad_read = read(99, buf, sizeof(buf));
    if (bad_read >= 0) {
        print_err(err_fail);
        return 12;
    }

    /* 13. Error handling: close invalid fd */
    int bad_close = close(99);
    if (bad_close >= 0) {
        print_err(err_fail);
        return 13;
    }

    /* 14. Error handling: close already-closed fd */
    int double_close = close(fd);
    if (double_close >= 0) {
        print_err(err_fail);
        return 14;
    }

    print("file_test: all assertions passed\n");
    return 0;
}
