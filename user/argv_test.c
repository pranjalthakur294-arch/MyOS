/*
 * user/argv_test.c - Ring 3 Argument Vector Verification Utility
 *
 * Inspects and prints argc, each argv string, and validates null-termination of argv.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    print("argc: ");
    print_dec((int64_t)argc);
    print("\n");

    for (int i = 0; i < argc; i++) {
        print("argv[");
        print_dec((int64_t)i);
        print("]: '");
        if (argv[i] != NULL) {
            print(argv[i]);
        } else {
            print("(NULL)");
        }
        print("'");
        if (argc > 16) {
            if ((i + 1) % 4 == 0 || i == argc - 1) {
                print("\n");
            } else {
                print(" ");
            }
        } else {
            print("\n");
        }
    }

    if (argv[argc] == NULL) {
        print("argv[argc] is NULL: yes\n");
    } else {
        print("argv[argc] is NULL: no\n");
    }

    return 0;
}
