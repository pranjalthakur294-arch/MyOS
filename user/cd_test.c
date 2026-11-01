/*
 * user/cd_test.c - Userland chdir/getcwd Syscall Verification Program
 *
 * Verifies:
 *   1. getcwd() verifies initial directory is "/"
 *   2. chdir("/disk") succeeds and getcwd() returns "/disk"
 *   3. chdir("bin") from /disk resolves relatively to "/disk/bin"
 *   4. chdir("..") returns to parent ("/disk")
 *   5. chdir to non-existent directory returns negative error
 *   6. chdir to a regular file (/readme.txt) returns negative error (-ENOTDIR)
 *   7. getcwd with too-small buffer returns negative error (-ERANGE)
 *   8. Prints "cd_test: all assertions passed\n" and exits with 0
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

volatile uint64_t cd_test_data = 0x8877665544332211ULL;

int64_t user_main(void) {
    (void)cd_test_data;

    print("cd_test: starting chdir & getcwd tests...\n");

    char buf[128];
    memset(buf, 0, sizeof(buf));

    /* 1. Initial directory must be "/" */
    if (getcwd(buf, sizeof(buf)) == NULL) {
        print_err("cd_test: FAIL initial getcwd returned NULL\n");
        return 1;
    }
    if (strcmp(buf, "/") != 0) {
        print_err("cd_test: FAIL initial directory is not /\n");
        return 2;
    }

    /* 2. chdir("/disk") succeeds, getcwd() returns "/disk" */
    int rc = chdir("/disk");
    if (rc != 0) {
        print_err("cd_test: FAIL chdir(/disk)\n");
        return 3;
    }
    memset(buf, 0, sizeof(buf));
    if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/disk") != 0) {
        print_err("cd_test: FAIL getcwd after chdir(/disk)\n");
        return 4;
    }

    /* 3. chdir("bin") relative navigation */
    rc = chdir("bin");
    if (rc != 0) {
        print_err("cd_test: FAIL chdir(bin)\n");
        return 5;
    }
    memset(buf, 0, sizeof(buf));
    if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/disk/bin") != 0) {
        print_err("cd_test: FAIL getcwd after chdir(bin)\n");
        return 6;
    }

    /* 4. chdir("..") returns to parent "/disk" */
    rc = chdir("..");
    if (rc != 0) {
        print_err("cd_test: FAIL chdir(..)\n");
        return 7;
    }
    memset(buf, 0, sizeof(buf));
    if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/disk") != 0) {
        print_err("cd_test: FAIL getcwd after chdir(..)\n");
        return 8;
    }

    /* 5. chdir to non-existent directory returns negative error */
    rc = chdir("/nonexistent_directory_9999");
    if (rc >= 0) {
        print_err("cd_test: FAIL chdir nonexistent dir succeeded\n");
        return 9;
    }

    /* 6. chdir to regular file returns negative error (-ENOTDIR) */
    rc = chdir("/readme.txt");
    if (rc >= 0) {
        print_err("cd_test: FAIL chdir to regular file succeeded\n");
        return 10;
    }
    if (rc != -ENOTDIR) {
        print_err("cd_test: FAIL chdir to regular file did not return -ENOTDIR\n");
        return 11;
    }

    /* 7. getcwd with too-small buffer returns NULL / -ERANGE */
    char small_buf[2];
    if (getcwd(small_buf, sizeof(small_buf)) != NULL) {
        print_err("cd_test: FAIL getcwd with small buffer succeeded\n");
        return 12;
    }
    int64_t raw_ret = __syscall2(SYS_GETCWD, (int64_t)small_buf, sizeof(small_buf));
    if (raw_ret != -ERANGE) {
        print_err("cd_test: FAIL raw sys_getcwd did not return -ERANGE\n");
        return 13;
    }

    print("cd_test: all assertions passed\n");
    return 0;
}
